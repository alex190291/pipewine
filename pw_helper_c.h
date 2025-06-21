#pragma once

#include "pw_helper_common.h"
#include <pipewire/filter.h>

// Forward declarations for PipeWire types
struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_filter;
struct pw_node;

// PipeWire filter state enum



#ifdef __cplusplus
extern "C" {
#endif

struct user_pw_helper;

// Operation types for deferred PipeWire operations
enum pw_op_type {
    PW_OP_NONE = 0,
    PW_OP_CREATE_PORTS,
    PW_OP_CONNECT_FILTER,
    PW_OP_CLEANUP_FILTER
};

struct user_pw_helper *user_pw_create_helper(int argc, char **argv, struct pw_helper_init_args const *conf);
void user_pw_destroy_helper(struct user_pw_helper *helper);

struct pw_node *user_pw_get_default_node(struct user_pw_helper *helper, enum spa_direction direction);
struct pw_node *user_pw_find_node_by_name(struct user_pw_helper *helper, char const *name);

void user_pw_lock_loop(struct user_pw_helper *helper);
void user_pw_unlock_loop(struct user_pw_helper *helper);

// Device hot-plug monitoring (Task 3.1)
typedef void (*user_pw_device_callback_t)(struct pw_node *node, int added, void *userdata);

void user_pw_set_device_callback(struct user_pw_helper *helper,
                                 user_pw_device_callback_t cb,
                                 void *userdata);

// Worker thread management for deferred operations
int user_pw_schedule_work(struct user_pw_helper *helper, enum pw_op_type operation, void *userdata);

// C bridge function for calling ASIO driver worker functions from C++ code
typedef int (*pw_worker_callback_t)(void *userdata, enum pw_op_type operation);
void user_pw_set_worker_callback(pw_worker_callback_t callback);

// Event processing and filter state management
void user_pw_trigger_event_processing(struct user_pw_helper *helper);
int user_pw_wait_for_filter_state(struct user_pw_helper *helper, struct pw_filter *filter, enum pw_filter_state target_state, int timeout_ms);

// Endpoint enumeration for GUI library
struct pw_node **user_pw_enumerate_endpoints(struct user_pw_helper *helper, int *count);
void user_pw_free_endpoints(struct pw_node **endpoints);

#ifdef __cplusplus
}
#endif
