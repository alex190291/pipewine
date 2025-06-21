#pragma once

#include <pipewire/node.h>
#include <pipewire/filter.h>
#include <pipewire/context.h>
#include <string>
#include <vector>
#include <span>
#include <functional>

#include "pw_helper_common.h"

namespace PwHelper {

struct Helper;
typedef struct pw_helper_init_args InitArgs;

// Core helper functions
Helper *create_helper(int argc, char **argv, InitArgs const *conf);
void destroy_helper(Helper *helper);

// Node enumeration and selection
std::vector<struct pw_node *> enumerate_pipewire_endpoints(Helper *helper);
struct pw_node *get_default_node(Helper *helper, enum spa_direction direction);
struct pw_node *find_node_by_name(Helper *helper, char const *name);

// Enhanced node property access
void get_node_props(Helper *helper, struct pw_node *proxy, std::vector<std::pair<std::string_view, std::string*> > const& props);

// Stream management
struct pw_filter *create_filter(Helper *helper, const char *name, struct pw_properties *props);
int connect_filter_ports(Helper *helper, struct pw_filter *filter, struct pw_node *target_node);

// Buffer and format management
int set_filter_format(Helper *helper, struct pw_filter *filter, uint32_t sample_rate, uint32_t channels);
int get_buffer_size(Helper *helper, struct pw_filter *filter);
int set_buffer_size(Helper *helper, struct pw_filter *filter, uint32_t size);

// Callback management
typedef std::function<void(void *data, struct spa_io_position *position)> ProcessCallback;
void set_process_callback(Helper *helper, struct pw_filter *filter, ProcessCallback callback, void *user_data);

// Thread safety
void lock_loop(Helper *helper);
void unlock_loop(Helper *helper);

// Error handling
const char *get_last_error(Helper *helper);

// Device monitoring
typedef std::function<void(struct pw_node *node, bool added)> DeviceCallback;
void set_device_callback(Helper *helper, DeviceCallback callback);

}
