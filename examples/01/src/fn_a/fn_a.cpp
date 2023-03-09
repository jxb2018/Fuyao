//
// Created by tank on 8/16/22.
//

#include "func_worker_interface.h"

#include <string.h>
#include "utils.h"
#include "base/logging.h"
#include <fmt/core.h>
#include <fmt/format.h>

struct worker_context {
    void *caller_context;
    faas_invoke_func_fn_t invoke_func_fn;
    faas_append_output_fn_t append_output_fn;
};

int faas_init() {
    return 0;
}

int faas_create_func_worker(void *caller_context,
                            faas_invoke_func_fn_t invoke_func_fn,
                            faas_append_output_fn_t append_output_fn,
                            void **worker_handle) {
    auto context = new worker_context;

    context->caller_context = caller_context;
    context->invoke_func_fn = invoke_func_fn;
    context->append_output_fn = append_output_fn;

    *worker_handle = context;
    return 0;
}

int faas_func_call(void *worker_handle,
                   const char *input,
                   size_t input_length) {
    auto context = reinterpret_cast<struct worker_context *>(worker_handle);

    // parse request
    long will_generate_payload_size = utils::parse_payload_size(input);
    if (will_generate_payload_size == -1) {
        perror("parse request failed!");
        return -1;
    }

    // generate payload
    void *generate_payload;

    int res = posix_memalign(&generate_payload, 4096, will_generate_payload_size + 1);
    if (generate_payload == nullptr || res != 0) {
        perror("generate payload failed!");
        return -1;
    }

    char *request_payload = new char[will_generate_payload_size + 1];
    memset(request_payload, 1, will_generate_payload_size);
    request_payload[input_length] = '\0';

    // for response
    char *buf = (char *) malloc(1024);
    int nrecved;
    const char *response_payload = "";

    // invoke fn_c via inter-host function call
    auto start_time = utils::get_timestamp_us();

    std::string gateway_addr, gateway_port;
    gateway_addr = utils::GetEnvVariable("LUMINE_GATEWAY_ADDR", "192.168.1.198");
    gateway_port = utils::GetEnvVariable("LUMINE_GATEWAY_PORT", "8082");

    auto request = utils::Socket(gateway_addr.c_str(), std::stoi(gateway_port));
    utils::HttpParser *http_parser = nullptr;

    if (request.conn() < 0) {
        perror("Failed to connect");
        return -1;
    }

    request.issue_http_request("POST", "/function/exp01Fne", request_payload);
    nrecved = request.recv_response(buf, 1024);

    http_parser = new utils::HttpParser(buf, nrecved);
    response_payload = http_parser->extract_payload();

    auto end_time = utils::get_timestamp_us();

    char fn_a_output[60];

    sprintf(fn_a_output, "Via Gateway: interaction latency : %ld\n", end_time - start_time);

    // send the output back to the engine
    context->append_output_fn(context->caller_context, fn_a_output, strlen(fn_a_output));

    free(buf);
    free(request_payload);
    delete http_parser;
    return 0;
}

int faas_destroy_func_worker(void *worker_handle) {
    auto context = reinterpret_cast<struct worker_context *>(worker_handle);
    delete context;
    return 0;
}