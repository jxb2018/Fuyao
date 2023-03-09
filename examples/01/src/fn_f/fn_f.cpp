//
// Created by tank on 8/16/22.
//

#include "func_worker_interface.h"

#include <string.h>
#include <stdio.h>

#include "utils.h"

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

    const char *fn_e_output;
    size_t fn_e_output_length;
    // parse request
    long will_generate_payload_size = utils::parse_payload_size(input);
    if(will_generate_payload_size == -1){
        perror("parse request failed!");
        return -1;
    }

    // generate payload
    void *generate_payload;

    int res = posix_memalign(&generate_payload, 4096, will_generate_payload_size);
    if(generate_payload == nullptr || res != 0){
        perror("generate payload failed!");
        return -1;
    }
    memset(generate_payload,1,will_generate_payload_size);

    // invoke fn_b via fast internal function call
    auto start_time = utils::get_timestamp_us();

    int ret = context->invoke_func_fn(context->caller_context,
                                      "exp01Fne",
                                      reinterpret_cast<char *>(generate_payload),
                                      will_generate_payload_size,
                                      &fn_e_output, &fn_e_output_length, PassingMethod::DRC_OVER_Fabric);
    auto end_time = utils::get_timestamp_us();

    if(ret != 0){
        return -1;
    }
    char fn_c_output[60];
    sprintf(fn_c_output, "Via DRC_OVER_Fabric: interaction latency : %ld\n", end_time - start_time);

    // send the output back to the engine
    context->append_output_fn(context->caller_context, fn_c_output, strlen(fn_c_output));

    free(generate_payload);
    return 0;
}

int faas_destroy_func_worker(void *worker_handle) {
    auto context = reinterpret_cast<struct worker_context *>(worker_handle);
    delete context;
    return 0;
}