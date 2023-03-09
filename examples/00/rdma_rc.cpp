#include "utils.h"
#include <unistd.h>
#include <getopt.h>
#include <string>

#include "rdma/infinity.h"
#include "rdma/queue_pair.h"
#include "rdma/shared_memory.h"

struct Config {
    // socket
    std::string ip_addr;
    int tcp_port;

    // rdma
    std::string device_name;
    int ib_port;
    int ib_gid_idx;

    // type
    bool is_server;
} config_{};

struct RDMAInfo {
    faas::rdma::QueuePairInfo qp_info;
    faas::rdma::MemoryRegionInfo mr_info;
} self_rdma_info_{}, peer_rdma_info_{};

enum RDMA_OP {
    RDMA_WRITE,
    RDMA_READ,
    RDMA_SEND,
    RDMA_RECV
};

faas::rdma::Infinity *infinity_;
faas::rdma::QueuePair *qp_;
faas::rdma::SharedMemory *shared_memory_;

char recv_buf[1024];

utils::Socket *socket_;

void prepare_rdma_info() {
    infinity_ = new faas::rdma::Infinity(config_.device_name, config_.ib_port, config_.ib_gid_idx);
    // prepare queue pair
    qp_ = infinity_->CreateQueuePair();
    auto qp_info = qp_->GetQueuePairInfo();
    // prepare rdma shared memory
    shared_memory_ = new faas::rdma::SharedMemory();
    auto shared_memory_info = shared_memory_->GetSharedMemoryInfo();
    qp_->RegisterMemoryRegion("SHARED_MEMORY", shared_memory_info.addr, shared_memory_info.length);
    auto mr_info = qp_->GetMemoryRegionInfo("SHARED_MEMORY");

    self_rdma_info_ = {
            .qp_info = qp_info,
            .mr_info = mr_info
    };
}

void parse_command_line(int argc, char *argv[]) {
    static struct option long_options[] = {
            {.name = "port", .has_arg = 1, .val = 'p'},
            {.name = "ib-dev", .has_arg = 1, .val = 'd'},
            {.name = "ib-port", .has_arg = 1, .val = 'i'},
            {.name = "gid-idx", .has_arg = 1, .val = 'g'},
            {.name = nullptr, .has_arg = 0, .val = '\0'}
    };
    int opt;

    while ((opt = getopt_long(argc, argv, "p:d:i:g:", long_options, nullptr)) != EOF) {
        switch (opt) {
            case 'p':
                config_.tcp_port = strtol(optarg, nullptr, 0);
                break;
            case 'd':
                config_.device_name = strdup(optarg);
                break;
            case 'i':
                config_.ib_port = strtoul(optarg, nullptr, 0);
                break;
            case 'g':
                config_.ib_gid_idx = strtoul(optarg, nullptr, 0);
                break;
            default:
                perror("invalid arguments");
                exit(-1);
        }
    }

    if (optind == argc - 1) {
        // this is client
        config_.ip_addr = argv[optind];
        config_.is_server = false;
    } else if (optind == argc) {
        // this is server
        config_.ip_addr = "0.0.0.0";
        config_.is_server = true;
    } else {
        perror("invalid input");
        exit(-1);
    }
}

void exchange_rdma_info() {
    if (config_.is_server) {
        socket_->listen_without_block();
        // waiting request
        int fd = socket_->waiting_request();
        int num_recv = read(fd, recv_buf, 1024);
        if (num_recv != sizeof(RDMAInfo)) {
            perror("server: invalid rdma info");
            exit(-1);
        }
        peer_rdma_info_ = *(RDMAInfo *) recv_buf;
        // send rdma info to client
        prepare_rdma_info();
        send(fd, &self_rdma_info_, sizeof(RDMAInfo), 0);
        close(fd);
    } else {
        int fd = socket_->conn();
        // send rdma info to server
        prepare_rdma_info();
        send(fd, &self_rdma_info_, sizeof(RDMAInfo), 0);
        // waiting response
        int num_recv = read(fd, recv_buf, 1024);
        if (num_recv != sizeof(RDMAInfo)) {
            perror("client: invalid rdma info");
            exit(-1);
        }
        peer_rdma_info_ = *(RDMAInfo *) recv_buf;
    }
}

void build_rdma_channel() {
    qp_->StateFromResetToInit();
    qp_->StateFromInitToRTR(peer_rdma_info_.qp_info.lid,
                            peer_rdma_info_.qp_info.qp_num,
                            peer_rdma_info_.qp_info.psn,
                            peer_rdma_info_.qp_info.gid);
    qp_->StateFromRTRToRTS();
}

long one_sided(uint32_t payload_size, RDMA_OP op) {
    auto start_time = utils::get_timestamp_us();
    auto wr = faas::rdma::MakeWorkRequest(self_rdma_info_.mr_info, peer_rdma_info_.mr_info, 0, 0, 0, payload_size);
    switch (op) {
        case RDMA_READ:
            qp_->PostReadRequest(wr);
            break;
        case RDMA_WRITE:
            qp_->PostWriteRequest(wr);
            break;
        default:
            break;
    }
    int poll_result;
    do {
        poll_result = qp_->PollCompletion([](uint64_t) {});
    } while (poll_result == 0);
    auto end_time = utils::get_timestamp_us();

    return end_time - start_time;
}

long two_sided(uint32_t payload_size, RDMA_OP op) {
    auto start_time = utils::get_timestamp_us();
    auto wr = faas::rdma::MakeWorkRequest(self_rdma_info_.mr_info, peer_rdma_info_.mr_info, 0, 0, 0, payload_size);
    switch (op) {
        case RDMA_SEND:
            qp_->SetNotifyNonBlock(faas::rdma::CompleteQueue::SEND_COMPLETE_QUEUE);
            qp_->PostSendRequest(wr);
            qp_->NotifyCompletion(faas::rdma::CompleteQueue::SEND_COMPLETE_QUEUE);
            break;
        case RDMA_RECV:
            qp_->SetNotifyNonBlock(faas::rdma::CompleteQueue::RECV_COMPLETE_QUEUE);
            qp_->PostRecvRequest(wr);
            qp_->NotifyCompletion(faas::rdma::CompleteQueue::RECV_COMPLETE_QUEUE);
            break;
        default:
            break;
    }
    auto end_time = utils::get_timestamp_us();
    return end_time - start_time;
}

void test_rdma_one_sided_latency(uint32_t payload_size) {
    long sum = 0, cnt = 100;
    for (int i = 0; i < cnt; i++) {
        sum += one_sided(payload_size, RDMA_READ);
    }
    printf("payload_size = %d, complete a RDMA READ request need %ld us\n", payload_size, sum / cnt);
}

void test_rdma_two_sided_latency(uint32_t payload_size) {
    long sum = 0, cnt = 100;
    if (config_.is_server) {
        for (int i = 0; i < cnt; i++){
            sum += two_sided(payload_size, RDMA_RECV);
        }
        printf("payload_size = %d, complete a RDMA RECV request need %ld us\n", payload_size, sum / cnt);
    } else {
        for (int i = 0; i < cnt; i++){
            usleep(50);
            sum += two_sided(payload_size, RDMA_SEND);
        }
        printf("payload_size = %d, complete a RDMA SEND request need %ld us\n", payload_size, sum / cnt);
    };
}

int main(int argc, char *argv[]) {
    // step one: parse command line
    parse_command_line(argc, argv);
    socket_ = new utils::Socket(config_.ip_addr.c_str(), config_.tcp_port);

    // step two: exchange rdma info by socket
    exchange_rdma_info();

    // step three: build rdma channel, and then enter rdma server status
    build_rdma_channel();
    sleep(1);

    // step four: perform some tests
    printf("perform test1: two-sided rdma\n");
    test_rdma_two_sided_latency(1024);
    sleep(2);

    printf("perform test2: one_sided rdma\n");
    test_rdma_one_sided_latency(1024);
    sleep(2);

    return 0;
}