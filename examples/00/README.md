# EXP00

In this experiment, we measure the end-to-end latency of RDMA operations.

## Installation

### Requirements

- machines equipped with BlueField DPUs

### Building

CLion recommended.

### Running

**Note:** `show_gids` is a nice script that provides you a summary table listing the device, port, index GID, IP
address,
RoCE version and the device name.

- first, start `exp00_rdma_server`  with the arguments

```
 -p <listen-port> -d <ib-dev> -i <ibv_port> -g <gid_idx>
```

- second, start `exp00_rdma_client`  with the arguments

```
 -p <listen-port> -d <ib-dev> -i <ib-port> -g <gid_idx> <server_ip>
```

## Evaluation

**Note:** payload size : 1024 Bytes, units: us

| -                               | READ/WRITE | SEND/RECV |
|---------------------------------|------------|-----------|
| Host to Host (single machine)   | 10         | 25        |
| Host to Host (Multiple machine) | 4          | 26        |
| DPU  to Host (single machine)   | 4          | 19        |
| DPU  to Host (Multiple machine) | 10         | 26        |