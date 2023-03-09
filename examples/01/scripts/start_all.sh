#!/bin/bash

source_dir=/home/lgw
build_dir=/tmp/Fuyao
output_dir=/tmp/exp01

#config for launcher
root_path_for_ipc_1=/dev/shm/ipc4
root_path_for_ipc_2=/dev/shm/ipc6
device_name=mlx5_0
device_port=1
device_gidx=3

# start gateway
${build_dir}/gateway/gateway \
  --config_file=${source_dir}/Fuyao/examples/01/service_config.json \
  1>${output_dir}/gateway.log 2>&1 &
sleep 2

# start engine
${build_dir}/engine/engine \
  --config_file=${source_dir}/Fuyao/examples/01/service_config.json \
  --guid=101 1>${output_dir}/engine1.log 2>&1 &

${build_dir}/engine/engine \
  --config_file=${source_dir}/Fuyao/examples/01/service_config.json \
  --guid=102 1>${output_dir}/engine2.log 2>&1 &

sleep 2

# start agent (only for test)
${build_dir}/agent/agent \
  --config_file=${source_dir}/Fuyao/examples/01/service_config.json \
  --guid=201 1>${output_dir}/agent1.log 2>&1 &

${build_dir}/agent/agent \
  --config_file=${source_dir}/Fuyao/examples/01/service_config.json \
  --guid=202 1>${output_dir}/agent2.log 2>&1 &

# start functions with launcher
## for engine 101
${build_dir}/launcher/launcher \
  --func_id=5 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_e \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_1} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_e_1.log 2>&1 &

${build_dir}/launcher/launcher \
  --func_id=2 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_b \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_1} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_b_1.log 2>&1 &

${build_dir}/launcher/launcher \
  --func_id=4 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_d \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_1} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_d_1.log 2>&1 &

## for engine 102
${build_dir}/launcher/launcher \
  --func_id=5 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_e \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_2} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_e_2.log 2>&1 &

${build_dir}/launcher/launcher \
  --func_id=2 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_b \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_2} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_b_2.log 2>&1 &

${build_dir}/launcher/launcher \
  --func_id=4 \
  --fprocess_mode=cpp \
  --fprocess=${build_dir}/examples/exp01/exp01_fn_d \
  --fprocess_output_dir=${output_dir} \
  --root_path_for_ipc=${root_path_for_ipc_2} \
  --device_name=${device_name} \
  --device_port=${device_port} \
  --device_gidx=${device_gidx} \
  1>${output_dir}/exp01_fn_d_2.log 2>&1 &