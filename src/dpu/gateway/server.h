#ifndef LUMINE_SERVER_H
#define LUMINE_SERVER_H

#include "base/common.h"
#include "base/thread.h"
#include "base/macro.h"
#include "common/uv.h"
#include "common/stat.h"
#include "common/protocol.h"
#include "common/config.h"
#include "server/server_base.h"
#include "func_call_context.h"
#include "http_connection.h"
#include "grpc_connection.h"
#include "engine_connection.h"

namespace faas::gateway {

    class Server final : public server::ServerBase {
    public:
        static constexpr int kDefaultListenBackLog = 64;
        static constexpr int kDefaultNumIOWorkers = 1;

        Server();

        ~Server();

        void SetConfigFile(std::string_view path) {
            config_file_ = std::string(path);
        }

        config::Config *GetFuncConfig(){
            return &config_;
        }

        // Must be thread-safe
        void OnNewHttpFuncCall(HttpConnection *connection, FuncCallContext *func_call_context);

        void OnNewGrpcFuncCall(GrpcConnection *connection, FuncCallContext *func_call_context);

        void DiscardFuncCall(FuncCallContext *func_call_context);

        void OnRecvEngineMessage(EngineConnection *connection,
                                 const protocol::GatewayMessage &message,
                                 gsl::span<const char> payload);

    private:
        size_t max_running_requests_;
        std::string config_file_;
        config::Config config_;

        uv_tcp_t uv_engine_conn_handle_;
        uv_tcp_t uv_http_handle_;
        uv_tcp_t uv_grpc_handle_;
        std::vector<server::IOWorker *> io_workers_;

        size_t next_http_conn_worker_id_;
        size_t next_grpc_conn_worker_id_;
        int next_http_connection_id_;
        int next_grpc_connection_id_;

        class OngoingEngineHandshake;

        friend class OngoingEngineHandshake;

        absl::flat_hash_set<std::unique_ptr<OngoingEngineHandshake>> ongoing_engine_handshakes_;
        absl::flat_hash_map</* id */ int, std::shared_ptr<server::ConnectionBase>> engine_connections_;
        utils::BufferPool read_buffer_pool_;
        absl::flat_hash_set</* node_id */ uint16_t> connected_node_set_;

        std::atomic<uint32_t> next_call_id_;

        absl::Mutex mu_;
        std::vector</* node_id */ uint16_t> connected_nodes_ ABSL_GUARDED_BY(mu_);

        struct FuncCallState {
            protocol::FuncCall func_call;
            int connection_id;  // of HttpConnection or GrpcConnection
            FuncCallContext *context;
            int64_t recv_timestamp;
            int64_t dispatch_timestamp;
        };

        struct PerFuncStat {
            int64_t last_request_timestamp;
            stat::Counter incoming_requests_stat;
            stat::StatisticsCollector<int32_t> request_interval_stat;

            explicit PerFuncStat(uint16_t func_id);
        };

        absl::BitGen random_bit_gen_ ABSL_GUARDED_BY(mu_);
        absl::flat_hash_map</* func_id */ uint16_t, size_t>
                next_dispatch_node_idx_  ABSL_GUARDED_BY(mu_);
        absl::flat_hash_map</* func_id */ uint16_t, size_t>
                inflight_requests_per_node_  ABSL_GUARDED_BY(mu_);
        absl::flat_hash_map</* full_call_id */ uint64_t, FuncCallState>
                running_func_calls_ ABSL_GUARDED_BY(mu_);
        std::queue<FuncCallState> pending_func_calls_ ABSL_GUARDED_BY(mu_);
        absl::flat_hash_set</* full_call_id */ uint64_t>
                discarded_func_calls_ ABSL_GUARDED_BY(mu_);
        absl::flat_hash_map</* connection_id */ int,
                std::shared_ptr<server::ConnectionBase>>
                connections_ ABSL_GUARDED_BY(mu_);

        int64_t last_request_timestamp_ ABSL_GUARDED_BY(mu_);
        stat::Counter incoming_requests_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<int32_t> request_interval_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<float> requests_instant_rps_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<uint16_t> inflight_requests_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<uint16_t> running_requests_stat_ ABSL_GUARDED_BY(mu_);
        std::vector<std::unique_ptr<stat::Counter>> dispatched_requests_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<int32_t> queueing_delay_stat_ ABSL_GUARDED_BY(mu_);
        stat::StatisticsCollector<int32_t> dispatch_overhead_stat_ ABSL_GUARDED_BY(mu_);
        absl::flat_hash_map</* func_id */ uint16_t, std::unique_ptr<PerFuncStat>>
                per_func_stats_ ABSL_GUARDED_BY(mu_);

        void StartInternal() override;

        void StopInternal() override;

        void OnConnectionClose(server::ConnectionBase *connection) override;

        bool OnEngineHandshake(uv_tcp_t *uv_handle, gsl::span<const char> data);

        void OnNewFuncCallCommon(std::shared_ptr<server::ConnectionBase> parent_connection,
                                 FuncCallContext *func_call_context);

        void DispatchFuncCall(std::shared_ptr<server::ConnectionBase> parent_connection,
                              FuncCallContext *func_call_context, uint16_t node_id);

        void FinishFuncCall(std::shared_ptr<server::ConnectionBase> parent_connection,
                            FuncCallContext *func_call_context);

        void TickNewFuncCall(uint16_t func_id, int64_t current_timestamp)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

        uint16_t PickNextNode(const protocol::FuncCall &func_call) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

        DECLARE_UV_CONNECTION_CB_FOR_CLASS(HttpConnection);

        DECLARE_UV_CONNECTION_CB_FOR_CLASS(GrpcConnection);

        DECLARE_UV_CONNECTION_CB_FOR_CLASS(EngineConnection);

        DECLARE_UV_READ_CB_FOR_CLASS(ReadEngineHandshake);

        DISALLOW_COPY_AND_ASSIGN(Server);
    };

}  // namespace faas

#endif