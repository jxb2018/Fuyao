实验一
---
测试各种数据传输方式的性能

『注意』：本版本暂时不支持DRC_OVER_IPC, DRC_OVER_Fabric。该特性将在后续开源版本推出。

## 配置 

需要根据实际的运行环境修改`server_config.json`中的内容。

通常情况下，以下字段需要修改

```json
{
  "Gateway": {
    "gatewayIP": "x.x.x.x"
  },
  "Engines": [
    {
      "engineIP": "x.x.x.x",
      "deviceName": "x",
      "devicePort": 0,
      "deviceGidIndex": 0
    }
  ],
  "Agents": [
    {
      "deviceName": "x",
      "devicePort": 0,
      "deviceGidIndex": 0,
      "BindEngineGuid": 101
    }
  ]
}
```

说明： 
- 配置文件中，Gateway的IP需要设置正确，这关系到Engine是否能与Gateway建立连接。
- Engines字段是一个列表，与实际部署的节点数量相对应（默认情况一个节点一个Engine进程）。Agents与Engines同理。
- RDMA硬件的相关信息需要正确配置。使用`show_gids`命令查看。
- 在Agents中，每个Agent的BindEngineGuid字段需要正确配置。这个字段代表的是Agent与Engine的绑定关系。默认情况下，同一节点上的Engine与Agent相互绑定。

## 运行

『注意』：
- 测试IPC，可以仅启动 gateway，Engine，以及相应的函数。
- 测试Fabric，需要启动gateway，Engine，Agent，以及相应的函数。

### 启动 gateway

```shell
/tmp/Fuyao/gateway/gateway --config_file=/home/lgw/Fuyao/examples/01/service_config.json
```

### 启动Engine

按照`server_config.json` 在对应节点上开启Engine

```shell
/tmp/Fuyao/engine/engine --config_file=/home/lgw/Fuyao/examples/01/service_config.json --guid=101
```

### 启动测试函数

本版本暂不支持节点间可用列表同步，测试的时候，需要在每个节点都运行测试函数。

函数的启动方式有两种：
- Debug模式启动

```shell
# 以启动fn_e为例，其余同理
/tmp/Fuyao/examples/exp01/exp01_fn_e --debug_file_path=/home/lgw/Fuyao/examples/01/debug/fn_e.json
```
- Launcher模式启动

```shell
# 以启动fn_e为例，其余同理
/tmp/Fuyao/launcher/launcher 
--func_id=5
--fprocess_mode=cpp
--fprocess="/tmp/Fuyao/examples/exp01/exp01_fn_e" 
--fprocess_output_dir="/tmp"
--root_path_for_ipc="/dev/shm/ipc4"
--device_name="mlx5_0"
--device_port=1
--device_gidx=3
```

『区别』：Debug模式仅仅支持单实例，Launcher则可以根据配置文件启动相应数量的函数实例。

### 在DPU上启动 Agent

```shell

```

## 测试

```shell
# 测试 fn_e
curl -X POST -d 16B http://127.0.0.1:8084/function/exp01Fne

# 测试 IPC
curl -X POST -d 16B http://127.0.0.1:8084/function/exp01Fnb

# 测试 Fabric
curl -X POST -d 16B http://127.0.0.1:8084/function/exp01Fnd
```