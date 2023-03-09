//
// Created by tank on 8/16/22.
//

#ifndef LUMINE_BENCHMARKS_UTILS_H
#define LUMINE_BENCHMARKS_UTILS_H

#include <sys/types.h>
#include <ctime>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <fmt/core.h>
#include <fmt/format.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "http_parser.h"

namespace utils {

    long get_timestamp_us() {
        struct timespec time_stamp;

        clock_gettime(CLOCK_REALTIME, &time_stamp);

        long timestamp_us = time_stamp.tv_sec * 1000000 + time_stamp.tv_nsec / 1000;
        return timestamp_us;
    }

    inline std::string GetEnvVariable(std::string name,
                                      std::string default_value = "") {
        char *value = getenv(std::string(name).c_str());
        return value != nullptr ? value : default_value;
    }

/*
 * return payload size(B) or -1
 *
 * format:
 *  1B 1KB 1MB 1GB
 * */
    long parse_payload_size(const char *input) {
        long payload_size = 0;
        const char *ptr = input;
        char prefix = '&';
        bool end_flag = false;

        // remove the space at the beginning
        for (; *ptr; ptr++) {
            if (*ptr == ' ')
                continue;
            break;
        }

        for (; char ch = *ptr; ptr++) {
            if (end_flag) break;

            switch (ch) {
                case 'B':
                    switch (prefix) {
                        case '&':
                            payload_size *= 1;
                            break;
                        case 'K':
                            payload_size *= 1024;
                            break;
                        case 'M':
                            payload_size *= (1024 * 1024);
                            break;
                        case 'G':
                            payload_size *= (1024 * 1024 * 1024);
                            break;
                    }
                    end_flag = true;
                    break;
                case 'K':
                case 'M':
                case 'G':
                    prefix = ch;
                    break;
                default:
                    if (ch <= '9' && ch >= '0') {
                        payload_size = payload_size * 10 + (ch - '0');
                    } else {
                        return -1;
                    }
                    break;
            }
        }

        for (; *ptr; ptr++) {
            if (*ptr == ' ')
                continue;
            return -1;
        }

        if (!end_flag) {
            return -1;
        }

        return payload_size;
    }

    class HttpParser {
    public:
        HttpParser(char *data, int len) : data_(data), len_(len) {
            http_parser_init(&http_parser_, HTTP_RESPONSE);
            http_parser_settings_init(&http_parser_settings_);
            http_parser_.data = this;

            http_parser_settings_.on_body = HttpParserOnBodyCallback;
        }

        ~HttpParser() {
            free(buf_);
        }

        const char *extract_payload() {
            int nparsed = http_parser_execute(&http_parser_, &http_parser_settings_, data_, len_);

            if (nparsed != len_) {
                perror("parse http request error \n");
                return nullptr;
            }
            buf_ = (char *) malloc(payload_length_ + 1);
            memcpy(buf_, payload_, payload_length_);
            buf_[payload_length_] = '\0';

            return buf_;
        }

    private:
        char *data_;
        size_t len_;

        const char *payload_;

        char *buf_;

        size_t payload_length_;

        http_parser http_parser_;
        http_parser_settings http_parser_settings_;

        // parse http request
        static int HttpParserOnBodyCallback(http_parser *parser, const char *data, size_t length) {
            auto self = reinterpret_cast<HttpParser *>(parser->data);
            self->OnHttpParser(data, length);
            return 0;
        }

        int OnHttpParser(const char *data, size_t length) {
            payload_ = data;
            payload_length_ = length;
            return 0;
        }
    };

    class Socket {
    public:
        Socket(const char *ip, uint16_t port) : ip_(ip), port_(port) {
            serv_addr_.sin_family = AF_INET;
            serv_addr_.sin_addr.s_addr = INADDR_ANY;
            serv_addr_.sin_port = htons(port);
        }

        ~Socket() {
            // closing the connected socket
            close(client_fd_);
        }

        // return fd
        int conn() {
            // socket creation
            if ((sock_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Socket creation error \n");
                return -1;
            }
            // Convert IPv4 and IPv6 addresses from text to binary
            if (inet_pton(AF_INET, ip_, &serv_addr_.sin_addr) <= 0) {
                perror("Invalid address/ Address not supported \n");
                return -1;
            }

            if ((client_fd_ = connect(sock_, (struct sockaddr *) &serv_addr_, sizeof(serv_addr_))) < 0) {
                perror("Connection Failed \n");
                return -1;
            }
            return sock_;
        }

        // non-block
        int listen_without_block() {
            // socket creation
            if ((sock_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Socket creation error \n");
                return -1;
            }

            // Set socket opt
            int opt = 1;
            if(setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, (char *) &opt, sizeof(int))!=0){
                perror("Set socket opt error \n");
                return -1;
            }

            // bind
            if(bind(sock_, (struct sockaddr *) &serv_addr_, sizeof(serv_addr_)) < 0){
                perror("Bind error \n");
                return -1;
            }

            // listen
            if(listen(sock_, SOMAXCONN) < 0) {
                perror("listen error \n");
                return -1;
            }

            return 0;
        }

        // return fd
        int waiting_request(){
            int addrlen = sizeof(serv_addr_);
            int new_socket = accept(sock_, (struct sockaddr *)&serv_addr_, (socklen_t *)&addrlen);
            return new_socket;
        }

        int issue_http_request(const char *method, const char *url_path, const char *payload) {
            std::string message = fmt::format("{} {} HTTP/1.1\n"
                                              "Host: {}:{}\n"
                                              "User-Agent: curl/7.74.0\n"
                                              "Accept: */*\n"
                                              "Content-Length: {}\n"
                                              "Content-Type: application/x-www-form-urlencoded\n"
                                              "\n"
                                              "{}", method, url_path, ip_, port_, strlen(payload), payload);

            const char *send_buf = message.c_str();
            size_t send_length = message.length();

            int nsent, total_nsent = 0;

            while (total_nsent < send_length) {
                nsent = send(sock_, send_buf + total_nsent, send_length - total_nsent, 0);
                total_nsent += nsent;
            }

            return 0;
        }

        int recv_response(char *recv_buf, int max_num_read) const {
            int num_recv = read(sock_, recv_buf, max_num_read);
            return num_recv;
        }

    private:
        int client_fd_, sock_;
        const char *ip_;
        uint16_t port_;
        struct sockaddr_in serv_addr_;
    };

    /**
     * return the number sent or -1
     *
     * example : exp01::utils::issue_http_request("POST", "localhost", 8080, "/function/exp01fnc", request_payload, buf, 1024)
     * **/
    int issue_http_request(const char *method, const char *ip, uint16_t port, const char *url_path, const char *payload,
                           char *recv_buf, int max_nread) {
        std::string message = fmt::format("{} {} HTTP/1.1\n"
                                          "Host: {}:{}\n"
                                          "User-Agent: curl/7.74.0\n"
                                          "Accept: */*\n"
                                          "Content-Length: {}\n"
                                          "Content-Type: application/x-www-form-urlencoded\n"
                                          "\n"
                                          "{}", method, url_path, ip, port, strlen(payload), payload);

        int sock, client_fd;
        struct sockaddr_in serv_addr;

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation error \n");
            return -1;
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        // Convert IPv4 and IPv6 addresses from text to binary
        // form
        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported \n");
            return -1;
        }

        if ((client_fd = connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0) {
            perror("Connection Failed \n");
            return -1;
        }
        send(sock, message.c_str(), message.length(), 0);

        int nrecvd = read(sock, recv_buf, max_nread);

        // closing the connected socket
        close(client_fd);

        return nrecvd;
    }

}

#endif //LUMINE_BENCHMARKS_UTILS_H
