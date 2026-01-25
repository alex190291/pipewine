#pragma once

/**
 * @file pw_helper_c.h
 * @brief C interface to PipeWire helper functionality
 *
 * This header provides the C API for the PipeWire ASIO driver to interact
 * with PipeWire. All functions are thread-safe unless noted otherwise.
 */

#include "pw_helper_common.h"
#include <pipewire/filter.h>

// Forward declarations for PipeWire types
struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_filter;
struct pw_node;

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

/**
 * @brief Create a new PipeWire helper instance
 * @param argc Argument count (usually 0)
 * @param argv Argument vector (usually NULL)
 * @param conf Configuration parameters
 * @return New helper instance, or NULL on failure
 */
struct user_pw_helper *user_pw_create_helper(int argc, char **argv, struct pw_helper_init_args const *conf);

/**
 * @brief Destroy a PipeWire helper instance
 * @param helper Instance to destroy (safe to pass NULL)
 */
void user_pw_destroy_helper(struct user_pw_helper *helper);

/**
 * @brief Get the default node for a direction
 * @param helper Helper instance
 * @param direction PipeWire direction
 * @return Default node or NULL if not available
 */
struct pw_node *user_pw_get_default_node(struct user_pw_helper *helper, enum spa_direction direction);

/**
 * @brief Try to get the default node without blocking
 * @param helper Helper instance
 * @param direction PipeWire direction
 * @return Default node or NULL if not ready
 */
struct pw_node *user_pw_get_default_node_nonblocking(struct user_pw_helper *helper, enum spa_direction direction);

/**
 * @brief Find a node by name
 * @param helper Helper instance
 * @param name Node name to search for
 * @return Node proxy or NULL if not found
 */
struct pw_node *user_pw_find_node_by_name(struct user_pw_helper *helper, char const *name);

/**
 * @brief Lock the helper's PipeWire loop mutex
 * @param helper Helper instance
 */
void user_pw_lock_loop(struct user_pw_helper *helper);

/**
 * @brief Unlock the helper's PipeWire loop mutex
 * @param helper Helper instance
 */
void user_pw_unlock_loop(struct user_pw_helper *helper);

// Device hot-plug monitoring (Task 3.1)
typedef void (*user_pw_device_callback_t)(struct pw_node *node, int added, void *userdata);

/**
 * @brief Register a device hot-plug callback
 * @param helper Helper instance
 * @param cb Callback or NULL to clear
 * @param userdata User-provided context passed to the callback
 */
void user_pw_set_device_callback(struct user_pw_helper *helper,
                                 user_pw_device_callback_t cb,
                                 void *userdata);

// Worker thread management for deferred operations
/**
 * @brief Schedule a deferred PipeWire operation
 * @param helper Helper instance
 * @param operation Operation type
 * @param userdata User data passed to the worker callback
 * @return 0 on success, negative on failure
 */
int user_pw_schedule_work(struct user_pw_helper *helper, enum pw_op_type operation, void *userdata);

// C bridge function for calling ASIO driver worker functions from C++ code
typedef int (*pw_worker_callback_t)(void *userdata, enum pw_op_type operation);

/**
 * @brief Register the worker callback invoked by deferred operations
 * @param callback Callback function or NULL to clear
 */
void user_pw_set_worker_callback(pw_worker_callback_t callback);

// Event processing and filter state management
/**
 * @brief Trigger PipeWire event processing
 * @param helper Helper instance
 */
void user_pw_trigger_event_processing(struct user_pw_helper *helper);

/**
 * @brief Wait for a filter to reach a target state
 * @param helper Helper instance
 * @param filter PipeWire filter to monitor
 * @param target_state Desired filter state
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if state reached, 0 on timeout or error
 */
int user_pw_wait_for_filter_state(struct user_pw_helper *helper, struct pw_filter *filter, enum pw_filter_state target_state, int timeout_ms);

// Endpoint enumeration for GUI library
/**
 * @brief Enumerate available PipeWire endpoints
 * @param helper Helper instance
 * @param count Output number of endpoints
 * @return Array of node pointers (free with user_pw_free_endpoints)
 */
struct pw_node **user_pw_enumerate_endpoints(struct user_pw_helper *helper, int *count);

/**
 * @brief Free endpoint array returned by user_pw_enumerate_endpoints
 * @param endpoints Endpoint array to free
 */
void user_pw_free_endpoints(struct pw_node **endpoints);

#ifdef __cplusplus
}
#endif
