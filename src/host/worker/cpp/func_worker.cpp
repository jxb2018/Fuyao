#include "func_worker.h"

#include "common/time.h"
#include "ipc/base.h"
#include "ipc/fifo.h"
#include "utils/io.h"
#include "utils/socket.h"
#include "runtime/worker_lib.h"
#include "base/common.h"
#include "base/logging.h"
#include "utils/docker.h"

#include <fmt/core.h>
#include <fmt/format.h>

#include <memory>

namespace faas::worker_cpp {

    using protocol::FuncCall;
    using protocol::NewFuncCall;
    using protocol::FuncCallDebugString;
    using protocol::Message;
    using protocol::GetFuncCallFromMessage;
    using protocol::IsHandshakeResponseMessage;
    using protocol::IsDispatchFuncCallMessage;
    using protocol::IsFuncCallCompleteMessage;
    using protocol::IsFuncCallFailedMessage;
    using protocol::NewFuncWorkerHandshakeMessage;
    using protocol::NewFuncCallFailedMessage;
    using protocol::NewLauncherHandshakeMessage;
    using protocol::IsRDMARequestMessage;
    using protocol::IsRDMAReclaimMessage;

    FuncWorker::FuncWorker()
            : func_id_(-1), fprocess_id_(-1), client_id_(0), message_pipe_fd_(-1),
              use_engine_socket_(false), engine_tcp_port_(-1), use_fifo_for_nested_call_(false),
              infinity_(nullptr), message_size_(1024),
              func_call_timeout_(kDefaultFuncCallTimeout), tmp_id_(0),
              engine_sock_fd_(-1), input_pipe_fd_(-1), output_pipe_fd_(-1), timeout_as_ms_for_drc_(-1),
              buffer_pool_for_pipes_("Pipes", PIPE_BUF), ongoing_invoke_func_(false),
              next_call_id_(0), current_func_call_id_(0), on_debug_(false) {}

    FuncWorker::~FuncWorker() {
        if (engine_sock_fd_ != -1) {
            close(engine_sock_fd_);
        }
        if (input_pipe_fd_ != -1 && !use_engine_socket_) {
            close(input_pipe_fd_);
        }
        if (output_pipe_fd_ != -1 && !use_engine_socket_) {
            close(output_pipe_fd_);
        }
        delete infinity_;
    }

    std::string FuncWorker::GetUniqueTmpName() {
        tmp_id_++;
        return "tmp-" + std::to_string(tmp_id_);
    }

    void FuncWorker::Serve() {
        CHECK(func_id_ != -1);
        CHECK(fprocess_id_ != -1);

        if (on_debug_) {
            engine_sock_fd_debug_ = GetEngineSockFd();

            Message handshake_message = NewLauncherHandshakeMessage(func_id_);
            std::string self_container_id = docker_utils::GetSelfContainerId();
            DCHECK_EQ(self_container_id.size(), docker_utils::kContainerIdLength);
            SetInlineDataInMessage(&handshake_message, std::span<const char>(self_container_id.data(),
                                                                             self_container_id.size()));
            PCHECK(io_utils::SendMessage(engine_sock_fd_debug_, handshake_message));
            Message response;
            CHECK(io_utils::RecvMessage(engine_sock_fd_debug_, &response, nullptr));
            size_t payload_size = response.payload_size;
            char *payload = new char[payload_size];
            CHECK(io_utils::RecvData(engine_sock_fd_debug_, payload, payload_size, nullptr));
            CHECK(config_.Load(std::string_view(payload, payload_size)))
            << "Failed to load function configs from payload";
            free(payload);

            Message first_pending_message;
            CHECK(io_utils::RecvMessage(engine_sock_fd_debug_, &first_pending_message, nullptr));
            client_id_ = first_pending_message.client_id;

        } else {
            uint32_t payload_size;
            char *payload;
            CHECK(io_utils::RecvData(message_pipe_fd_, reinterpret_cast<char *>(&payload_size),
                                     sizeof(uint32_t), /* eof= */ nullptr))
            << "Failed to receive payload size from launcher";

            payload = reinterpret_cast<char *>(malloc(payload_size));
            auto reclaim_payload_buffer = gsl::finally([payload] { free(payload); });
            CHECK(io_utils::RecvData(message_pipe_fd_, payload, payload_size, /* eof= */ nullptr))
            << "Failed to receive payload data from launcher";
            CHECK(config_.Load(std::string_view(payload, payload_size)))
            << "Failed to load function configs from payload";

            if (engine_tcp_port_ != -1) {
                faas_engine_host_ = utils::GetEnvVariable("FAAS_ENGINE_HOST", "127.0.0.1");
            }
        }
        CHECK(client_id_ > 0);
        LOG(INFO) << "My client_id is " << client_id_;
        HandshakeWithEngine();

        // Create RDMA context
#ifdef __ENABLE_RDMA
        infinity_ = new rdma::Infinity(rdma_device_name_, rdma_device_port_, rdma_device_gid_index_);
#else
        infinity_ = nullptr;
#endif
        // Pre-allocate
        // Initialize shared memory
        shared_memory_ = new rdma::SharedMemory();
        shared_memory_info_ = shared_memory_->GetSharedMemoryInfo();

        // Pre-allocate buf
        uint64_t alloc_addr;

        alloc_addr = shared_memory_->AllocateMemory(message_size_);
        buf_for_send_req_ = reinterpret_cast<char *>(alloc_addr);

        alloc_addr = shared_memory_->AllocateMemory(message_size_);
        buf_for_recv_req_ = reinterpret_cast<char *>(alloc_addr);

        // Enter main serving loop
        MainServingLoop();
    }

    void FuncWorker::MainServingLoop() {
        // Init func worker
        CHECK(faas_init() == 0) << "Failed to initialize loaded library";

        // Create func worker
        CHECK(faas_create_func_worker(this,
                                      &FuncWorker::InvokeFuncWrapper,
                                      &FuncWorker::AppendOutputWrapper,
                                      &worker_handle_) == 0)
        << "Failed to create function worker";

        if (!use_engine_socket_) {
            ipc::FifoUnsetNonblocking(input_pipe_fd_);
        }

        // Exec function
        while (true) {
            Message message;
            state_.store(WAITING);
            CHECK(io_utils::RecvMessage(input_pipe_fd_, &message, nullptr))
            << "Failed to receive message from engine";
            if (IsDispatchFuncCallMessage(message)) {
                state_.store(RUNNING);
                ExecuteFunc(message);
            } else {
                LOG(FATAL) << "Unknown message type";
            }
        }

        // Destroy func worker
        CHECK(faas_destroy_func_worker(worker_handle_) == 0)
        << "Failed to destroy function worker";
    }

    int FuncWorker::GetEngineSockFd() {
        int engine_sock_fd;
        if (engine_tcp_port_ == -1) {
            engine_sock_fd = utils::UnixDomainSocketConnect(ipc::GetEngineUnixSocketPath());
        } else {
            engine_sock_fd = utils::TcpSocketConnect(faas_engine_host_.c_str(), engine_tcp_port_);
        }
        CHECK(engine_sock_fd != -1) << "Failed to connect to engine socket";
        return engine_sock_fd;
    }

    void FuncWorker::HandshakeWithEngine() {
        engine_sock_fd_ = GetEngineSockFd();
        if (use_engine_socket_) {
            LOG(INFO) << "Use engine socket for messages";
            input_pipe_fd_ = engine_sock_fd_;
        } else {
            LOG(INFO) << "Use extra pipes for messages";
            input_pipe_fd_ = ipc::FifoOpenForRead(ipc::GetFuncWorkerInputFifoName(client_id_));
        }
        Message message = NewFuncWorkerHandshakeMessage(func_id_, client_id_);
        PCHECK(io_utils::SendMessage(engine_sock_fd_, message));
        Message response;
        CHECK(io_utils::RecvMessage(engine_sock_fd_, &response, nullptr))
        << "Failed to receive handshake response from engine";
        CHECK(IsHandshakeResponseMessage(response))
        << "Receive invalid handshake response";
        // set timeout for drc
        timeout_as_ms_for_drc_ = response.timeout_for_drc;
        CHECK(timeout_as_ms_for_drc_ != -1 && timeout_as_ms_for_drc_ != 0);
        if (use_engine_socket_) {
            output_pipe_fd_ = engine_sock_fd_;
        } else {
            output_pipe_fd_ = ipc::FifoOpenForWrite(ipc::GetFuncWorkerOutputFifoName(client_id_));
        }
        if (response.flags & protocol::kUseFifoForNestedCallFlag) {
            LOG(INFO) << "Use extra FIFOs for handling nested call";
            use_fifo_for_nested_call_ = true;
        }
        LOG(INFO) << "Handshake done";
    }

    AllocMRInfo FuncWorker::PostRecvRequest(std::string_view guid_name, const char **data, size_t *data_size) {
        DCHECK(queue_pairs_.contains(guid_name));

        auto qp = queue_pairs_[guid_name]->qp;
        auto mr_info = qp->GetMemoryRegionInfo(guid_name);

        rdma::WorkRequest wr = {
                .local = {
                        .mr_addr_start = reinterpret_cast<uint64_t>(buf_for_recv_req_),
                        .mr_lkey = mr_info.lkey
                },
                .size = message_size_
        };

        qp->SetNotifyNonBlock(rdma::CompleteQueue::RECV_COMPLETE_QUEUE);
        qp->PostRecvRequest(wr);
        qp->NotifyCompletion(rdma::CompleteQueue::RECV_COMPLETE_QUEUE);

        auto head_for_drc = reinterpret_cast<protocol::HeadForDRC *>(buf_for_recv_req_);
        *data_size = head_for_drc->data_size;

        uint64_t start_addr_alloc = 0;
        AllocMRType type;

        if (!protocol::IsDRCMetaData(*head_for_drc)) {
            *data = reinterpret_cast<const char *>(buf_for_recv_req_ + sizeof(protocol::HeadForDRC));
        } else {
            rdma::MemoryRegionInfo local_mr_info{};
            auto remote_mr_info =
                    reinterpret_cast<rdma::MemoryRegionInfo *>(buf_for_recv_req_ + sizeof(protocol::HeadForDRC));

            start_addr_alloc = shared_memory_->AllocateMemory(*data_size);

            void *buf;
            uint64_t local_offset;
            if (start_addr_alloc != 0) {
                local_mr_info = mr_info;
                buf = reinterpret_cast<void *>(start_addr_alloc);
                local_offset = start_addr_alloc - local_mr_info.addr;
                type = FromSharedMemory;
            } else {
                int res = posix_memalign(&buf, PAGE_SIZE, *data_size);
                DCHECK(res == 0 && buf != nullptr);

                std::string mr_name = GetUniqueTmpName();
                qp->RegisterMemoryRegion(mr_name, reinterpret_cast<uint64_t>(buf), *data_size);
                local_mr_info = qp->GetMemoryRegionInfo(mr_name);

                local_offset = 0;
                type = NewCreation;
            }

            rdma::WorkRequest new_wr = MakeWorkRequest(local_mr_info, *remote_mr_info,
                                                       local_offset, 0, 0, *data_size);

            qp->SetNotifyNonBlock(rdma::CompleteQueue::SEND_COMPLETE_QUEUE);
            qp->PostReadRequest(new_wr);
            qp->NotifyCompletion(rdma::CompleteQueue::SEND_COMPLETE_QUEUE);

            *data = reinterpret_cast<const char *>(local_mr_info.addr + local_offset);
        }

        return AllocMRInfo{.type = type, .addr = start_addr_alloc, .size=*data_size};
    }


    void FuncWorker::PostSendRequest(std::string_view guid_name, const char **data, size_t *data_size) {
        DCHECK(queue_pairs_.contains(guid_name));
        auto qp = queue_pairs_[guid_name]->qp;

        rdma::MemoryRegionInfo local_mr_info = qp->GetMemoryRegionInfo(guid_name);

        uint32_t size, expected_size;
        expected_size = *data_size + sizeof(protocol::HeadForDRC);

        if (expected_size > message_size_) {
            // Register new memory region
            auto start_addr_raw_data = reinterpret_cast<uint64_t>(*data);

            std::string mr_name = GetUniqueTmpName();
            qp->RegisterMemoryRegion(mr_name, start_addr_raw_data, *data_size);
            auto tmp_mr_info = qp->GetMemoryRegionInfo(mr_name);

            protocol::HeadForDRC head_for_drc = {
                    .drc_type = static_cast<uint16_t>(protocol::DRCType::METADATA),
                    .data_size = *data_size
            };

            memcpy(buf_for_send_req_, &head_for_drc, sizeof(protocol::HeadForDRC));
            memcpy(buf_for_send_req_ + sizeof(protocol::HeadForDRC), &tmp_mr_info, sizeof(rdma::MemoryRegionInfo));

            size = sizeof(protocol::HeadForDRC) + sizeof(rdma::MemoryRegionInfo);
        } else {
            protocol::HeadForDRC head_for_drc = {
                    .drc_type = static_cast<uint16_t>(protocol::DRCType::GENERAL),
                    .data_size = *data_size
            };

            memcpy(buf_for_send_req_, &head_for_drc, sizeof(protocol::HeadForDRC));
            memcpy(buf_for_send_req_ + sizeof(protocol::HeadForDRC), *data, *data_size);

            size = sizeof(protocol::HeadForDRC) + *data_size;
        }

        // make work request
        rdma::WorkRequest wr = {
                .local = {
                        .mr_addr_start = reinterpret_cast<uint64_t>(buf_for_send_req_),
                        .mr_lkey = local_mr_info.lkey
                },
                .size = size
        };
        qp->PollCompletion(nullptr, rdma::CompleteQueue::SEND_COMPLETE_QUEUE);
        qp->PostSendRequest(wr);

    }

    void FuncWorker::ReclaimDRCResources(std::string_view guid_name) {
        DCHECK(queue_pairs_.contains(guid_name));
        LOG(INFO) << "reclaim drc, guid_name = " << guid_name;

        // TODO: reclaim resources
        const char *input_data, *output_data;
        size_t input_length, output_length;

        input_length = 0;
        input_data = new char[message_size_];

        PostSendRequest(guid_name, &input_data, &input_length);

        delete input_data;
    }

    void FuncWorker::ExecuteFunc(const Message &dispatch_func_call_message) {
        DCHECK(state_.load() == RUNNING);

        int ret = 0, processing_time;
        const char *input_data, *output_data;
        char guid_name[128];
        size_t input_size, output_size;
        int64_t start_timestamp, current_timestamp;
        rdma::QueuePair *qp;
        Message response{};
        std::unique_ptr<ipc::ShmRegion> input_region;
        std::span<const char> input;
        State state;

        current_timestamp = GetMonotonicMicroTimestamp();

        auto dispatch_delay = gsl::narrow_cast<int32_t>(
                current_timestamp - dispatch_func_call_message.send_timestamp);

        FuncCall func_call = GetFuncCallFromMessage(dispatch_func_call_message);
        sprintf(guid_name, "%u-%d", func_call.engine_guid, func_call.client_id);

        if (!worker_lib::GetFuncCallInput(dispatch_func_call_message, &input, &input_region)) {
            response = NewFuncCallFailedMessage(func_call);
            response.send_timestamp = GetMonotonicMicroTimestamp();
            PCHECK(io_utils::SendMessage(output_pipe_fd_, response));
            return;
        }

        start_timestamp = GetMonotonicMicroTimestamp();
        current_func_call_id_.store(func_call.full_call_id);

        state = state_.load();
        func_output_buffer_.Reset();

        ret = faas_func_call(worker_handle_, input.data(), input.size());
        if (ret == -1) {
            LOG(ERROR) << "An error occurred during Func-worker execution";
        }
        ReclaimInvokeFuncResources();


        processing_time = gsl::narrow_cast<int32_t>(
                GetMonotonicMicroTimestamp() - start_timestamp);

        if (use_fifo_for_nested_call_) {
            worker_lib::FifoFuncCallFinished(
                    func_call, /* success= */ ret == 0, func_output_buffer_.to_span(),
                    processing_time, main_pipe_buf_, &response);
        } else {
            worker_lib::FuncCallFinished(
                    func_call, /* success= */ ret == 0, func_output_buffer_.to_span(),
                    processing_time, &response);
        }
        response.dispatch_delay = dispatch_delay;
        response.send_timestamp = GetMonotonicMicroTimestamp();
        PCHECK(io_utils::SendMessage(output_pipe_fd_, response));

    }

    bool FuncWorker::InvokeFunc(const char *func_name, const char *input_data, size_t input_length,
                                const char **output_data, size_t *output_length, PassingMethod method) {
//        auto program_start = GetMonotonicMicroTimestamp();
        // 1. retrieve function configuration
        const auto func_entry = config_.FindFunctionByFuncName(
                std::string_view(func_name, strlen(func_name)));
        if (func_entry == nullptr) {
            LOG(ERROR) << "Function " << func_name << " does not exist";
            return false;
        }

        state_.store(IPC_PROCESSING);

        std::string guid_name = func_name;
        bool result = true;
        rdma::QueuePair *qp;
        int64_t current_timestamp;

        // 2. new function call
        FuncCall func_call = NewFuncCall(
                gsl::narrow_cast<uint16_t>(func_entry->func_id),
                client_id_, next_call_id_.fetch_add(1));

        // 3. dispatch an invoke message via corresponding message-passing methods
        Message invoke_func_message{};
        std::unique_ptr<ipc::ShmRegion> input_region;

        if (state_.load() == IPC_PROCESSING) {
            // TODO: Need to optimize
            if (!worker_lib::PrepareNewFuncCall(
                    func_call, /* parent_func_call= */ current_func_call_id_.load(),
                    std::span<const char>(input_data, input_length),
                    &input_region, &invoke_func_message, method == PassingMethod::Fabric)) {
                return false;
            }
        }

        if (state_.load() == IPC_PROCESSING || state_.load() == RDMA_CREATING) {
            if (use_fifo_for_nested_call_) {
                result = FifoWaitInvokeFunc(&invoke_func_message, output_data, output_length);
            } else {
                result = WaitInvokeFunc(&invoke_func_message, output_data, output_length);
            }
        }

//        auto program_end = GetMonotonicMicroTimestamp();
//        LOG(INFO) << fmt::format("Completed an invocation, used {} us", program_end - program_start);
        return result;

    }

    bool FuncWorker::WaitInvokeFunc(Message *invoke_func_message,
                                    const char **output_data, size_t *output_length) {
        const FuncCall func_call = GetFuncCallFromMessage(*invoke_func_message);
        {
            absl::MutexLock lk(&mu_);
            if (ongoing_invoke_func_) {
                // TODO: fix this
                LOG(FATAL) << "NaiveWaitInvokeFunc cannot execute concurrently";
            }
            ongoing_invoke_func_ = true;
            invoke_func_message->send_timestamp = GetMonotonicMicroTimestamp();
            PCHECK(io_utils::SendMessage(output_pipe_fd_, *invoke_func_message));
        }
        Message result_message;
        CHECK(io_utils::RecvMessage(input_pipe_fd_, &result_message, nullptr));
        if (IsFuncCallFailedMessage(result_message)) {
            absl::MutexLock lk(&mu_);
            ongoing_invoke_func_ = false;
            return false;
        } else if (!IsFuncCallCompleteMessage(result_message)) {
            LOG(FATAL) << "Unknown message type";
        }
        InvokeFuncResource invoke_func_resource = {
                .func_call = func_call,
                .output_region = nullptr,
                .pipe_buffer = nullptr
        };
        if (result_message.payload_size < 0) {
            auto output_region = ipc::ShmOpen(
                    ipc::GetFuncCallOutputShmName(func_call.full_call_id));
            if (output_region == nullptr) {
                LOG(ERROR) << "ShmOpen failed";
                return false;
            }
            output_region->EnableRemoveOnDestruction();
            if (output_region->size() != gsl::narrow_cast<size_t>(-result_message.payload_size)) {
                LOG(ERROR) << "Output size mismatch";
                return false;
            }
            *output_data = output_region->base();
            *output_length = output_region->size();
            invoke_func_resource.output_region = std::move(output_region);
            absl::MutexLock lk(&mu_);
            invoke_func_resources_.push_back(std::move(invoke_func_resource));
            ongoing_invoke_func_ = false;
        } else {
            absl::MutexLock lk(&mu_);
            char *buffer;
            size_t size;
            buffer_pool_for_pipes_.Get(&buffer, &size);
            CHECK(size >= sizeof(Message));
            memcpy(buffer, &result_message, sizeof(Message));
            Message *message_copy = reinterpret_cast<Message *>(buffer);
            std::span<const char> output = GetInlineDataFromMessage(*message_copy);
            invoke_func_resource.pipe_buffer = buffer;
            *output_data = output.data();
            *output_length = output.size();
            invoke_func_resources_.push_back(std::move(invoke_func_resource));
            ongoing_invoke_func_ = false;
        }
        return true;
    }

    bool FuncWorker::FifoWaitInvokeFunc(Message *invoke_func_message,
                                        const char **output_data, size_t *output_length) {
        FuncCall func_call = GetFuncCallFromMessage(*invoke_func_message);
        // Create fifo for output
        if (!ipc::FifoCreate(ipc::GetFuncCallOutputFifoName(func_call.full_call_id))) {
            LOG(ERROR) << "FifoCreate failed";
            return false;
        }
        auto remove_output_fifo = gsl::finally([func_call] {
            ipc::FifoRemove(ipc::GetFuncCallOutputFifoName(func_call.full_call_id));
        });
        int output_fifo = ipc::FifoOpenForReadWrite(
                ipc::GetFuncCallOutputFifoName(func_call.full_call_id), /* nonblocking= */ true);
        if (output_fifo == -1) {
            LOG(ERROR) << "FifoOpenForReadWrite failed";
            return false;
        }
        auto close_output_fifo = gsl::finally([output_fifo] {
            if (close(output_fifo) != 0) {
                PLOG(ERROR) << "close failed";
            }
        });
        // Send message to engine (dispatcher)
        {
            absl::MutexLock lk(&mu_);
            invoke_func_message->send_timestamp = GetMonotonicMicroTimestamp();
            PCHECK(io_utils::SendMessage(output_pipe_fd_, *invoke_func_message));
        }
        VLOG(0) << "InvokeFuncMessage sent to engine";
        int timeout_ms = -1;
        if (func_call_timeout_ != absl::InfiniteDuration()) {
            timeout_ms = gsl::narrow_cast<int>(absl::ToInt64Milliseconds(func_call_timeout_));
        }
        if (!ipc::FifoPollForRead(output_fifo, timeout_ms)) {
            LOG(ERROR) << "FifoPollForRead failed";
            return false;
        }
        char *pipe_buffer;
        {
            absl::MutexLock lk(&mu_);
            size_t size;
            buffer_pool_for_pipes_.Get(&pipe_buffer, &size);
            DCHECK(size == PIPE_BUF);
        }
        std::unique_ptr<ipc::ShmRegion> output_region;
        bool success = false;
        bool pipe_buffer_used = false;
        std::span<const char> output;
        if (worker_lib::FifoGetFuncCallOutput(
                func_call, output_fifo, pipe_buffer,
                &success, &output, &output_region, &pipe_buffer_used)) {
            absl::MutexLock lk(&mu_);
            InvokeFuncResource invoke_func_resource = {
                    .func_call = func_call,
                    .output_region = nullptr,
                    .pipe_buffer = nullptr
            };
            if (pipe_buffer_used) {
                invoke_func_resource.pipe_buffer = pipe_buffer;
            } else {
                buffer_pool_for_pipes_.Return(pipe_buffer);
            }
            if (output_region != nullptr) {
                invoke_func_resource.output_region = std::move(output_region);
            }
            invoke_func_resources_.push_back(std::move(invoke_func_resource));
            if (success) {
                *output_data = output.data();
                *output_length = output.size();
                return true;
            } else {
                return false;
            }
        } else {
            absl::MutexLock lk(&mu_);
            buffer_pool_for_pipes_.Return(pipe_buffer);
            return false;
        }
    }

    void FuncWorker::ReclaimInvokeFuncResources() {
        absl::MutexLock lk(&mu_);
        for (const auto &resource: invoke_func_resources_) {
            if (resource.pipe_buffer != nullptr) {
                buffer_pool_for_pipes_.Return(resource.pipe_buffer);
            }
        }
        invoke_func_resources_.clear();


        for (auto &alloc_info: alloc_infos_) {
            if (alloc_info.type == FromSharedMemory) {
                if (alloc_info.addr)
                    shared_memory_->ReturnMemory(alloc_info.addr, alloc_info.size);
            } else {
                free(reinterpret_cast<void *>(alloc_info.addr));
            }
        }
        alloc_infos_.clear();
    }

    void FuncWorker::AppendOutputWrapper(void *caller_context, const char *data, size_t length) {
        FuncWorker *self = reinterpret_cast<FuncWorker *>(caller_context);
        self->func_output_buffer_.AppendData(data, length);
    }

    int FuncWorker::InvokeFuncWrapper(void *caller_context, const char *func_name,
                                      const char *input_data, size_t input_length,
                                      const char **output_data, size_t *output_length, PassingMethod method) {
        *output_data = nullptr;
        *output_length = 0;
        FuncWorker *self = reinterpret_cast<FuncWorker *>(caller_context);
        bool success = self->InvokeFunc(func_name, input_data, input_length,
                                        output_data, output_length, method);
        return success ? 0 : -1;
    }

}  // namespace faas
