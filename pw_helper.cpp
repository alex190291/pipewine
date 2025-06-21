#include "pw_helper.hpp"
#include "pw_helper_c.h"
#include "pw_helper_common.h"

#include <chrono>
#include <memory>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <cassert>

#include <thread>
#include <unordered_map>
#include <condition_variable>

#include <spa/utils/dict.h>
#include <spa/utils/json.h>
#include <spa/pod/builder.h>
#include <pipewire/context.h>
#include <pipewire/core.h>
#include <pipewire/keys.h>
#include <pipewire/main-loop.h>
#include <pipewire/node.h>
#include <pipewire/pipewire.h>
#include <pipewire/properties.h>
#include <pipewire/proxy.h>
#include <pipewire/thread.h>
#include <pipewire/thread-loop.h>
#include <pipewire/extensions/metadata.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <bit>

// Global worker callback for C/C++ bridge
static pw_worker_callback_t g_worker_callback = nullptr;

namespace PwHelper {

struct SpaPod {
	struct spa_pod *ptr;

	~SpaPod() {
		std::free(ptr);
	}

	constexpr explicit SpaPod(struct spa_pod *pod = nullptr)
		: ptr(pod) {}

	inline SpaPod(SpaPod &&o) : ptr(o.ptr) { o.ptr = nullptr; }

	inline SpaPod &operator=(SpaPod&& o) {
		std::swap(ptr, o.ptr);
		o = nullptr;
		return *this;
	}

	inline SpaPod &operator=(std::nullptr_t) {
		std::free(ptr);
		ptr = nullptr;
		return *this;
	}

	inline operator struct spa_pod const *() const {
		return ptr;
	}

	static SpaPod make(struct spa_pod const *pod) {
		return SpaPod(spa_pod_copy(pod));
	}

	// Explicit static method to avoid copying by accident
	static SpaPod copy(SpaPod const &from) {
		return make(from.ptr);
	}
};

// Source: <https://en.cppreference.com/w/cpp/container/unordered_map/find#Example>
struct string_view_hasher {
	using hash_type = std::hash<std::string_view>;
	using is_transparent = void;

	std::size_t operator()(const char* str) const        { return hash_type{}(str); }
	std::size_t operator()(std::string_view str) const   { return hash_type{}(str); }
	std::size_t operator()(std::string const& str) const { return hash_type{}(str); }
};

using string_map = std::unordered_map<std::string, std::string, string_view_hasher, std::equal_to<>>;

enum class InitState {
	Init,
	Ready,
	Running,
};

enum class PwInterface {
	Unknown,
	Node,
	Metadata,
};

enum class ProxyState {
	Init,
	PropsFilled,
	Fetching,
	UpdateInProgress,
};

template <typename Custom>
struct ProxyPtr {
	typename Custom::ProxyType *proxy;

	__always_inline constexpr ProxyPtr(typename Custom::ProxyType *proxy = {}) : proxy{proxy} {}

	__always_inline static ProxyPtr from_bound(void *bound) {
		return ProxyPtr{reinterpret_cast<typename Custom::ProxyType *>(bound)};
	}

	__always_inline operator typename Custom::ProxyType *() const {
		return proxy;
	}

	__always_inline Custom *custom() const {
		return reinterpret_cast<Custom *>(pw_proxy_get_user_data(reinterpret_cast<struct pw_proxy *>(proxy)));
	}

	template <typename TBase>
	__always_inline operator ProxyPtr<TBase>() const {
		static_assert(std::is_base_of_v<TBase, Custom>, "TBase must be a base of Custom");
		return ProxyPtr<TBase>{reinterpret_cast<typename TBase::ProxyType *>(proxy)};
	}

	template <typename TDerived>
	__always_inline ProxyPtr<TDerived> to_derived() const {
		static_assert(std::is_base_of_v<Custom, TDerived>, "Custom must be a base of TDerived");
		return ProxyPtr<TDerived>{reinterpret_cast<typename TDerived::ProxyType *>(proxy)};
	}

	template <typename TOther>
	__always_inline bool operator<(ProxyPtr<TOther> other) const {
		return reinterpret_cast<struct pw_proxy *>(proxy) < reinterpret_cast<struct pw_proxy *>(other.proxy);
	}

	template <typename TOther>
	__always_inline bool operator==(ProxyPtr<TOther> other) const {
		return reinterpret_cast<struct pw_proxy *>(proxy) == reinterpret_cast<struct pw_proxy *>(other.proxy);
	}

	__always_inline PwInterface type() const;
};

struct Proxy {
	using ProxyType = struct pw_proxy;
	PwInterface type;
};

template <typename T>
__always_inline PwInterface ProxyPtr<T>::type() const {
	return ProxyPtr<struct Proxy>(*this).custom()->type;
}

struct Node final: Proxy {
	using ProxyType = struct pw_node;

	std::atomic<ProxyState> info_state;
	std::atomic<ProxyState> param_state;
	struct spa_hook listener;
	struct pw_node_info info;
	string_map properties;
	std::unordered_map<uint32_t, SpaPod> params;

	static struct pw_node_events const s_events;

	void init(ProxyPtr<Node> proxy) {
		new (this) Node;
		Proxy::type = PwInterface::Node;
		info_state.store(ProxyState::Init, std::memory_order_relaxed);
		param_state.store(ProxyState::Init, std::memory_order_relaxed);
		listener = {};
		struct pw_node *raw_proxy = proxy;
		pw_node_add_listener(raw_proxy, &listener, &s_events, raw_proxy);
		pw_node_enum_params(raw_proxy, 0, PW_ID_ANY, 0, ~(uint32_t)0, nullptr);
	}

	// Bad name
	bool inited(std::mutex& mutex) {
		auto state = info_state.load(std::memory_order_relaxed);
		switch (state) {
			case ProxyState::Init:
			case ProxyState::Fetching:
				// For C++17 compatibility, we'll just return false and let caller retry
				return false;
			case ProxyState::PropsFilled:
			case ProxyState::UpdateInProgress:
				return true;
		}
		return false; // Should never reach here, but satisfy compiler
	}

	void get_or_wait_for_info(
		struct pw_node_info *out_info,
		string_map *all_props,
		std::vector<std::pair<std::string_view, std::string *> > const& props
	) {
		// Simplified for C++17 compatibility - just try to read current state
		ProxyState state = info_state.load(std::memory_order_relaxed);
		if (state == ProxyState::Init) {
			return; // Not ready yet
		}

		// Our turn to read
		if (out_info)
			*out_info = info;
		
		if (all_props) {
			// Copy all props
			*all_props = properties;
		}
		if (!props.empty()) {
			// Copy requested ones
			for (auto it = props.begin(); it != props.end(); ++it) {
				if (auto prop = properties.find(it->first); prop != properties.end()) {
					*it->second = prop->second;
				}
			}
		}
	}

	#if 0
	SpaPod get_or_wait_for_param(uint32_t id) {
		// Wait for init
		// TODO: Handle multi-step init
		param_state.wait(ProxyState::Init, std::memory_order_relaxed);
		// Then for updates
		ProxyState state = ProxyState::PropsFilled;
		while (!param_state.compare_exchange_weak(state, ProxyState::Fetching)) {
			assert(state == ProxyState::UpdateInProgress);
			param_state.wait(ProxyState::UpdateInProgress);
		}

		// Our turn to read
		if (auto it = params.find(name); it != params.end())
			SpaPod pod = SpaPod::copy();

		param_state.store(ProxyState::PropsFilled, std::memory_order_release);
		param_state.notify_one();
		return pod;
	}
	#endif

	void update(struct pw_node_info const *new_info) {
		// Simplified for C++17 compatibility
		info = *new_info;
		properties.clear();
		for (auto *it = info.props->items, *end = it + info.props->n_items; it != end; ++it) {
			std::string key = it->key;
			std::string value = it->value;
			properties.emplace(std::move(key), std::move(value));
		}
		info_state.store(ProxyState::PropsFilled, std::memory_order_release);
	}

	void update_param(void *proxy, uint32_t id, uint32_t index, uint32_t next, struct spa_pod const *param) {
		// Simplified for C++17 compatibility
		SpaPod pod = SpaPod::make(param);
		if (auto it = params.find(id); it != params.end()) {
			it->second = std::move(pod);
		} else {
			params.emplace(id, std::move(pod));
			//pw_node_subscribe_params(proxy, &id, 1);
		}

		param_state.store(ProxyState::PropsFilled, std::memory_order_release);
	}
};

static void node_info_handler(void *proxy, struct pw_node_info const *info) {
	ProxyPtr<Node>::from_bound(proxy).custom()->update(info);
}

static void node_param_handler(void *proxy, int seq, uint32_t id, uint32_t index, uint32_t next, struct spa_pod const *param) {
	ProxyPtr<Node>::from_bound(proxy).custom()->update_param(proxy, id, index, next, param);
}

struct pw_node_events const Node::s_events = {
	.version = PW_VERSION_NODE_EVENTS,
	.info = node_info_handler,
	.param = node_param_handler,
};

struct Metadata: Proxy {
	using ProxyType = struct pw_metadata;

	struct spa_hook listener;
	struct MetadataHandler const *vtable;

	static struct pw_metadata_events const s_events;

	void init(ProxyPtr<Metadata> proxy) {
		Proxy::type = PwInterface::Metadata;
		vtable = nullptr;
		struct pw_metadata *raw_proxy = proxy;
		pw_metadata_add_listener(raw_proxy, &listener, &s_events, raw_proxy);
	}

	inline ~Metadata();
};

enum MetaPropResult {
	Continue, // continue searching
	Stop, // stop search
};

struct MetadataProp {
	uint32_t subject;
	char const *key;
	char const *type;
	char const *value;

	// more data...
};

struct MetadataPropHandler {
	char const *key; // null if any
	char const *type; // null if any
	size_t proc_size;
	MetaPropResult (*handler)(ProxyPtr<Metadata> proxy, MetadataPropHandler const *handler, MetadataProp *prop);
};

struct MetadataHandler {
	MetadataPropHandler const *const *properties;
	size_t num_properties;
	// Called when specific properties were not satisfied.
	void (*generic_prop)(ProxyPtr<Metadata> proxy, uint32_t subject, char const *key, char const *type, char const *value);
	void (*destroy)(Metadata *self);
};

Metadata::~Metadata() {
	if (vtable)
		vtable->destroy(this);
}

static int meta_property_handler(void *proxy, uint32_t subject, char const *key, char const *type, char const *value) {
	auto mproxy = ProxyPtr<Metadata>::from_bound(proxy);
	Metadata *meta = mproxy.custom();
	if (!meta->vtable) {
		return 0;
	}
	std::string_view svkey = key;
	std::string_view svtype = type;
	//std::printf("[DEBUG] Got property '%s' of type '%s'\n", key, type);
	for (size_t idx = 0; idx != meta->vtable->num_properties; ++idx) {
		auto const *prop = meta->vtable->properties[idx];
		if (prop->key and svkey != prop->key)
			continue;
		if (prop->type and svtype != prop->type)
			continue;

		//std::printf("[DEBUG] Handling property '%s' of type '%s'\n", key, type);

		MetadataProp *scratch = reinterpret_cast<MetadataProp *>(std::malloc(prop->proc_size));
		scratch->subject = subject;
		scratch->key = key;
		scratch->type = type;
		scratch->value = value;
		auto res = prop->handler(mproxy, prop, scratch);
		std::free(scratch);
		switch (res) {
			case MetaPropResult::Continue: break;
			case MetaPropResult::Stop: return 0;
		}
	}
	if (meta->vtable->generic_prop) {
		meta->vtable->generic_prop(mproxy, subject, key, type, value);
	}
	return 0;
}

struct pw_metadata_events const Metadata::s_events = {
	.version = PW_VERSION_METADATA_EVENTS,
	.property = meta_property_handler,
};

struct MetadataJson: MetadataProp {
	char pod_buffer[0x800];
};

#define SPA_TYPE_STRING_JSON SPA_TYPE_INFO_BASE "String:JSON"

struct MetadataJsonHandler: MetadataPropHandler {
	//JsonType const *type;
	MetaPropResult (*handler)(ProxyPtr<Metadata> proxy, MetadataJsonHandler const *handler, MetadataJson *prop, struct spa_json *parser);
};

static MetaPropResult meta_preproc_json(ProxyPtr<Metadata> proxy, MetadataPropHandler const *handler, MetadataProp *prop) {
	auto *jhandler = static_cast<MetadataJsonHandler const *>(handler);
	MetadataJson *json = static_cast<MetadataJson *>(prop);
	struct spa_json parser;
	spa_json_init(&parser, prop->value, std::strlen(prop->value));
	return jhandler->handler(proxy, jhandler, json, &parser);
}

static bool parse_json_dict(struct spa_json *parser, char *heap, size_t heap_size, char const * const* keys, char ** values, size_t num_entries) {
	char const *token;
	struct spa_json obj;
	int l;
	size_t entries = num_entries;
	if (spa_json_enter_object(parser, &obj) <= 0)
		return false;
	while ((l = spa_json_get_string(&obj, heap, heap_size)) > 0) {
		std::string_view key = heap;
		assert(key.size() + 1 <= heap_size);
		for (size_t idx = 0; idx < entries; ++idx) {
			if (keys[idx] == key and !values[idx]) {
				l = spa_json_get_string(&obj, heap, heap_size);
				if (l <= 0)
					return false;
				std::string_view value = heap;
				assert(value.size() + 1 <= heap_size);

				values[idx] = heap;
				heap += value.size() + 1;
				heap_size -= value.size() + 1;
				goto next_item;
			}
		}

		// Skip value
		l = spa_json_next(&obj, &token);
		if (l <= 0)
			return false;
		else if (l > 0) {
			if (spa_json_is_container(token, l)) {
				struct spa_json sub;
				spa_json_enter(&obj, &sub);
				while ((l = spa_json_next(&sub, &token) > 0)) {}
				if (l < 0)
					return false;
			}
		}

		next_item:;
	}
	if (l < 0)
		return false;

	return true;
}

struct DefaultNodes: Metadata {
	std::mutex mutex;
	std::string default_source;
	std::string default_sink;

	static MetadataHandler const s_handler;

	void init(ProxyPtr<DefaultNodes> proxy) {
		Metadata::init(proxy);
		new (this) DefaultNodes;
		Metadata::vtable = &s_handler;
	}
};

static char const *default_nodes_keys[] = {
	"name",
};

static MetaPropResult default_nodes_source_handler(ProxyPtr<Metadata> proxy, MetadataJsonHandler const*, MetadataJson *prop, struct spa_json *parser) {
	char const *source;
	char *values[1] = {};
	if (!parse_json_dict(parser, prop->pod_buffer, sizeof prop->pod_buffer, default_nodes_keys, values, 1)) {
		std::fputs("[DEBUG] Invalid JSON dict\n", stderr);
	}

	source = values[0];
	if (source) {
		auto nodes = proxy.to_derived<DefaultNodes>().custom();
		nodes->mutex.lock();
		nodes->default_source.assign(source);
		nodes->mutex.unlock();
		//std::printf("[DEBUG] Found default source: %s\n", source);
	} else {
		std::fputs("[DEBUG] Default source node not found in metadata\n", stderr);
	}
	return MetaPropResult::Stop;
}

static MetaPropResult default_nodes_sink_handler(ProxyPtr<Metadata> proxy, MetadataJsonHandler const*, MetadataJson *prop, struct spa_json *parser) {
	char const *sink;
	char *values[1] = {};
	if (!parse_json_dict(parser, prop->pod_buffer, sizeof prop->pod_buffer, default_nodes_keys, values, 1)) {
		std::fputs("[DEBUG] Invalid JSON dict\n", stderr);
	}

	sink = values[0];
	if (sink) {
		auto nodes = proxy.to_derived<DefaultNodes>().custom();
		nodes->mutex.lock();
		nodes->default_sink.assign(sink);
		nodes->mutex.unlock();
		//std::printf("[DEBUG] Found default sink: %s\n", sink);
	} else {
		std::fputs("[DEBUG] Default sink node not found in metadata\n", stderr);
	}
	return MetaPropResult::Stop;
}

static MetadataJsonHandler const default_nodes_source_prop {
	MetadataPropHandler {
		.key = "default.audio.source",
		.type = SPA_TYPE_STRING_JSON,
		.proc_size = sizeof(MetadataJson),
		.handler = meta_preproc_json,
	},
	default_nodes_source_handler,
};

static MetadataJsonHandler const default_nodes_sink_prop {
	MetadataPropHandler {
		.key = "default.audio.sink",
		.type = SPA_TYPE_STRING_JSON,
		.proc_size = sizeof(MetadataJson),
		.handler = meta_preproc_json,
	},
	default_nodes_sink_handler,
};

static MetadataPropHandler const *const default_nodes_props[] = {
	&default_nodes_source_prop,
	&default_nodes_sink_prop,
};

MetadataHandler const DefaultNodes::s_handler = {
	.properties = default_nodes_props,
	.num_properties = std::size(default_nodes_props),
	.generic_prop = nullptr,
	.destroy = [] (Metadata *self) { static_cast<DefaultNodes *>(self)->~DefaultNodes(); },
};

struct Helper {
	//struct pw_main_loop *main_loop = {};
	struct pw_thread_loop *thread_loop = {};
	struct pw_context *context = {};
	struct pw_core *core = {};
	struct pw_registry *registry = {};
	struct spa_hook registry_listener = {};

	struct spa_thread_utils *thread_impl = {};
	pw_helper_thread_creator_t thread_creator = {};
	struct spa_thread_utils thread_utils;

	std::unordered_map<uint32_t, ProxyPtr<Proxy>> bound_proxies;
	ProxyPtr<DefaultNodes> default_nodes = {};

	std::atomic<InitState> init_state = InitState::Init;
	std::atomic<int> roundtrip_state = -1;
	std::mutex state_mutex;

	struct spa_hook roundtrip = {};

	// Device hot-plug callback (optional)
	PwHelper::DeviceCallback device_cb;

	// Worker system for deferred operations
	std::atomic<bool> worker_running = false;
	std::mutex worker_mutex;
	std::condition_variable worker_cond;
	enum pw_op_type pending_operation = PW_OP_NONE;
	void *operation_userdata = nullptr;

	~Helper() {
		if (core) {
			pw_core_disconnect(core);
		}
		if (context) {
			pw_context_destroy(context);
		}
		if (thread_loop) {
			pw_thread_loop_destroy(thread_loop);
		}
	}

	void stop() {
		pw_thread_loop_stop(this->thread_loop);
	}

	void lock() {
		if (init_state.load(std::memory_order_relaxed) == InitState::Running) {
			state_mutex.lock();
		}
	}

	void unlock() {
		if (init_state.load(std::memory_order_relaxed) == InitState::Running) {
			state_mutex.unlock();
		}
	}

	void wait_for_roundtrip() {
		// Simplified for C++17 compatibility - just check state
		while (init_state.load(std::memory_order_relaxed) != InitState::Ready) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void process_pending_operations() {
		std::unique_lock<std::mutex> lock(worker_mutex);
		printf("Worker: process_pending_operations called, pending_operation=%d\n", pending_operation);
		if (pending_operation != PW_OP_NONE && operation_userdata) {
			// Store operation info before calling the worker
			enum pw_op_type current_op = pending_operation;
			void *asio_driver = operation_userdata;
			
			// Reset operation state first
			pending_operation = PW_OP_NONE;
			operation_userdata = nullptr;
			
			// Unlock before invoking to avoid deadlocks
			lock.unlock();
			
			// Execute the worker callback in the PipeWire thread loop context
			int result = 0;
			if (g_worker_callback) {
				printf("Worker: Scheduling C worker callback for operation %d in PipeWire thread\n", current_op);
				printf("Worker: thread_loop=%p, init_state=%d\n", thread_loop, (int)init_state.load());
				
				// Create a structure to pass data to the invoke callback
				struct WorkerInvokeData {
					pw_worker_callback_t callback;
					void *userdata;
					enum pw_op_type operation;
					int result;
				} invoke_data = {
					.callback = g_worker_callback,
					.userdata = asio_driver,
					.operation = current_op,
					.result = 0
				};
				
				// Lock the PipeWire thread loop before calling pw_loop_invoke
				printf("Worker: Locking PipeWire thread loop\n");
				pw_thread_loop_lock(thread_loop);
				
				// Execute in PipeWire thread loop context
				printf("Worker: Calling pw_loop_invoke\n");
				pw_loop_invoke(pw_thread_loop_get_loop(thread_loop),
					[](struct spa_loop *loop, bool async, uint32_t seq, const void *data, size_t size, void *user_data) -> int {
						(void)loop; (void)async; (void)seq; (void)size; // Suppress unused parameter warnings
						const WorkerInvokeData *invoke_data = static_cast<const WorkerInvokeData*>(data);
						printf("Worker: Executing callback in PipeWire thread context\n");
						// We need to modify the result, but data is const, so we use user_data
						WorkerInvokeData *mutable_data = static_cast<WorkerInvokeData*>(user_data);
						mutable_data->result = invoke_data->callback(invoke_data->userdata, invoke_data->operation);
						printf("Worker: Callback completed with result %d\n", mutable_data->result);
						return 0;
					},
					SPA_ID_INVALID, // No sequence number needed
					&invoke_data, // data parameter
					sizeof(invoke_data), // size parameter
					true, // block until completion
					&invoke_data); // user_data parameter
				
				printf("Worker: pw_loop_invoke completed\n");
				
				// Unlock the PipeWire thread loop
				pw_thread_loop_unlock(thread_loop);
				printf("Worker: Unlocked PipeWire thread loop\n");
				
				result = invoke_data.result;
				if (result != 0) {
					printf("Worker: C worker callback failed with error: %d\n", result);
				} else {
					printf("Worker: C worker callback completed successfully\n");
				}
			} else {
				printf("Worker: No C worker callback registered for operation %d\n", current_op);
			}
			
			// Notify waiting threads
			worker_cond.notify_all();
		} else {
			printf("Worker: No pending operation to process\n");
		}
	}

	// Add method to trigger event processing for filter state transitions
	void trigger_event_processing() {
		if (thread_loop && core && init_state.load(std::memory_order_relaxed) == InitState::Running) {
			// Use pw_core_sync to force event processing - this is more reliable than pw_thread_loop_signal
			pw_thread_loop_lock(thread_loop);
			pw_core_sync(core, PW_ID_CORE, 0);
			pw_thread_loop_unlock(thread_loop);
		}
	}

	// Add method to wait for filter state transitions with timeout
	bool wait_for_filter_state_transition(struct pw_filter *filter, enum pw_filter_state target_state, int timeout_ms = 5000) {
		if (!filter) return false;
		
		auto start_time = std::chrono::steady_clock::now();
		auto timeout_duration = std::chrono::milliseconds(timeout_ms);
		
		printf("Waiting for filter state transition to %s (timeout: %d ms)\n", 
		                 pw_filter_state_as_string(target_state), timeout_ms);
		
		int iteration = 0;
		enum pw_filter_state last_state = PW_FILTER_STATE_ERROR;
		
		// Adaptive timing based on timeout duration - more aggressive for longer timeouts
		bool is_long_timeout = timeout_ms > 10000;
		int initial_fast_iterations = is_long_timeout ? 200 : 100;
		int fast_delay_ms = is_long_timeout ? 2 : 5;
		int slow_delay_ms = is_long_timeout ? 5 : 10;
		
		while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
			enum pw_filter_state current_state = pw_filter_get_state(filter, nullptr);
			
			// Log state changes
			if (current_state != last_state) {
				printf("Filter state changed: %s -> %s (iteration %d)\n", 
				                 pw_filter_state_as_string(last_state), 
				                 pw_filter_state_as_string(current_state), iteration);
				last_state = current_state;
			}
			
			if (current_state == target_state) {
				printf("Filter reached target state %s after %d iterations\n", 
				                 pw_filter_state_as_string(target_state), iteration);
				return true;
			}
			
			if (current_state == PW_FILTER_STATE_ERROR) {
				printf("Filter entered error state during state transition wait\n");
				return false;
			}
			
			// More aggressive event processing for faster state transitions
			if (iteration < initial_fast_iterations) {
				trigger_event_processing();
				// Very short delay for initial iterations
				std::this_thread::sleep_for(std::chrono::milliseconds(fast_delay_ms));
			} else {
				// Less frequent processing after initial period
				if (iteration % 15 == 0) {
					trigger_event_processing();
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(slow_delay_ms));
			}
			
			iteration++;
		}
		
		enum pw_filter_state final_state = pw_filter_get_state(filter, nullptr);
		printf("Filter state transition timeout after %d iterations. Final state: %s, target: %s\n", 
		                 iteration, pw_filter_state_as_string(final_state), 
		                 pw_filter_state_as_string(target_state));
		return false;
	}

	PwInterface get_proxy(uint32_t id, ProxyPtr<Proxy> &proxy) {
		if (auto it = bound_proxies.find(id); it != bound_proxies.end()) {
			proxy = it->second;
			return proxy.type();
		}
		return PwInterface::Unknown;
	}
};

#include "pw_thread.inc.cpp"

using namespace std::string_view_literals;
#define _SV(x) x##sv
#define SV(x) _SV(x)

static PwInterface get_known_interface(char const *type) {
	std::string_view svtype = type;
	if (svtype == SV(PW_TYPE_INTERFACE_Node)) {
		return PwInterface::Node;
	}
	if (svtype == SV(PW_TYPE_INTERFACE_Metadata)) {
		return PwInterface::Metadata;
	}
	return PwInterface::Unknown;
}

static void registry_global_handler(
	void *data, uint32_t id, uint32_t permissions,
	char const *type, uint32_t version, struct spa_dict const *props
) {
	Helper *This = reinterpret_cast<Helper *>(data);
	PwHelper::DeviceCallback cb;
	struct pw_node *added_node = nullptr;
	switch (get_known_interface(type)) {
		case PwInterface::Node: {
			This->lock();
			auto proxy = ProxyPtr<Node>::from_bound(
				pw_registry_bind(This->registry, id, type, std::min(version, (uint32_t)PW_VERSION_NODE), sizeof(Node)));
			proxy.custom()->init(proxy);
			This->bound_proxies.emplace(id, proxy);
			cb = This->device_cb;
			added_node = proxy;
			This->unlock();
			break;
		}
		case PwInterface::Metadata: {
			if ("default"sv == spa_dict_lookup(props, PW_KEY_METADATA_NAME)) {
				This->lock();
				auto proxy = ProxyPtr<DefaultNodes>::from_bound(
					pw_registry_bind(This->registry, id, type, std::min(version, (uint32_t)PW_VERSION_METADATA), sizeof(DefaultNodes)));
				proxy.custom()->init(proxy);
				This->bound_proxies.emplace(id, proxy);
				if (This->default_nodes) {
					std::puts("New default nodes object? Overriding old one.");
				}
				This->default_nodes = proxy;
				This->unlock();
			}
			break;
		}
		case PwInterface::Unknown: break;
	}
	if (cb && added_node) {
		cb(added_node, true);
	}
}

static void registry_global_remove_handler(void *data, uint32_t id) {
	Helper *This = reinterpret_cast<Helper *>(data);
	ProxyPtr<Proxy> global;
	PwHelper::DeviceCallback cb;
	struct pw_node *removed_node = nullptr;
	This->lock();
	switch (This->get_proxy(id, global)) {
		case PwInterface::Node:
			removed_node = global.to_derived<Node>();
			global.to_derived<Node>().custom()->~Node();
			goto destroy_proxy;
		case PwInterface::Metadata:
			if (This->default_nodes == global) {
				This->default_nodes = nullptr;
			}
			global.to_derived<Metadata>().custom()->~Metadata();
			goto destroy_proxy;

		destroy_proxy:
			This->bound_proxies.erase(id);
			pw_proxy_destroy(global);
			cb = This->device_cb;
			break;
		case PwInterface::Unknown: break;
	}
	This->unlock();
	if (cb && removed_node) {
		cb(removed_node, false);
	}
}

static struct pw_registry_events const s_registry_events = {
	.version = PW_VERSION_REGISTRY_EVENTS,
	.global = registry_global_handler,
	.global_remove = registry_global_remove_handler,
};

static void roundtrip_handler(void *data, uint32_t id, int seq) {
	Helper *This = reinterpret_cast<Helper *>(data);
	if (false) {
		// This is to test whether the initialization code propely waits for the roundtrip.
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}
	This->init_state.store(InitState::Running, std::memory_order_relaxed);
	// Removed notify_all for C++17 compatibility
}

static struct pw_core_events const s_core_events = {
	.version = PW_VERSION_CORE_EVENTS,
	.done = roundtrip_handler,
};

Helper *create_helper(int argc, char **argv, InitArgs const *conf) {
	pw_init(&argc, &argv);
	printf("PipeWire initialized with version: %s\n", pw_get_library_version());

	std::unique_ptr<Helper> This(new Helper);

	if (!(This->thread_loop = pw_thread_loop_new("pw-loop", NULL)))
	{
		std::fputs("Unable to create the PipeWire loop\n", stderr);
		return nullptr;
	}

	struct pw_properties *init_props = pw_properties_new(
		PW_KEY_CLIENT_NAME, "pw-asio",
		PW_KEY_CLIENT_API, "ASIO",
		nullptr);
	if (conf->app_name) {
		pw_properties_set(init_props, PW_KEY_APP_NAME, conf->app_name);
	}

	if (!(This->context = pw_context_new(
		pw_thread_loop_get_loop(This->thread_loop),
		init_props, 0)))
	{
		std::fputs("Unable to create a PipeWire context\n", stderr);
		return nullptr;
	}

	if (conf->thread_creator) {
		This->thread_creator = conf->thread_creator;
	}

	This->thread_impl = reinterpret_cast<struct spa_thread_utils *>(pw_context_get_object(This->context, SPA_TYPE_INTERFACE_ThreadUtils));
	if (!This->thread_impl) {
		This->thread_impl = pw_thread_utils_get();
	}
	This->thread_utils.iface = SPA_INTERFACE_INIT(
			SPA_TYPE_INTERFACE_ThreadUtils,
			SPA_VERSION_THREAD_UTILS,
			&thread_utils_impl, This.get());
	pw_context_set_object(This->context, SPA_TYPE_INTERFACE_ThreadUtils, &This->thread_utils);

	if (!(This->core = pw_context_connect(This->context, NULL, 0)))
	{
		std::fputs("Unable to connect to a PipeWire server\n", stderr);
		return nullptr;
	}

	if (!(This->registry = pw_core_get_registry(This->core, PW_VERSION_REGISTRY, 0)))
	{
		std::fputs("Unable to get the PipeWire registry\n", stderr);
		return nullptr;
	}

	pw_registry_add_listener(This->registry, &This->registry_listener, &s_registry_events, This.get());

	// Make sure to get all registered nodes before the client asks for them. (could be done
	// at a later point, but let's just wait now)
	pw_core_add_listener(This->core, &This->roundtrip, &s_core_events, This.get());

	This->init_state.store(InitState::Ready, std::memory_order_relaxed);

	This->roundtrip_state.store(0, std::memory_order_relaxed);
	pw_core_sync(This->core, PW_ID_CORE, 0);

	std::puts("[DEBUG] Starting thread");
	if (pw_thread_loop_start(This->thread_loop)) {
		std::fputs("Unable to start the PipeWire loop\n", stderr);
		return nullptr;
	}

	This->wait_for_roundtrip();
	std::puts("[DEBUG] Rountrip done");

	if (conf->loop)
		*conf->loop = pw_thread_loop_get_loop(This->thread_loop);
	if (conf->context)
		*conf->context = This->context;
	if (conf->core)
		*conf->core = This->core;

	return This.release();
}

void destroy_helper(Helper *helper) {
	helper->stop();
	delete helper;
}

std::vector<struct pw_node *> enumerate_pipewire_endpoints(Helper *helper) {
	std::vector<struct pw_node *> nodes;
	helper->lock();
	for (auto it = helper->bound_proxies.begin(), end = helper->bound_proxies.end(); it != end; ++it) {
		if (it->second.type() == PwInterface::Node) {
			nodes.push_back(it->second.to_derived<Node>());
		}
	}
	helper->unlock();
	return nodes;
}

static struct pw_node *find_node_by_name_locked(Helper *helper, std::string_view name) {
	std::string nod_name;
	std::vector<std::pair<std::string_view, std::string*> > props {
			{PW_KEY_NODE_NAME, &nod_name},
	};
	struct pw_node *found = nullptr;
	for (auto it = helper->bound_proxies.begin(), end = helper->bound_proxies.end(); it != end; ++it) {
		if (it->second.type() == PwInterface::Node) {
			it->second.to_derived<Node>().custom()->get_or_wait_for_info(nullptr, nullptr, props);
			if (nod_name == name) {
				found = it->second.to_derived<Node>();
				break;
			}
		}
	}
	return found;
}

static void wait_for_nodes_init(Helper *helper, std::mutex &mutex) {
	retry:
	for (auto it = helper->bound_proxies.begin(), end = helper->bound_proxies.end(); it != end; ++it) {
		if (it->second.type() == PwInterface::Node) {
			if (!it->second.to_derived<Node>().custom()->inited(mutex)) {
				goto retry;
			}
		}
	}
}

struct pw_node *get_default_node(Helper *helper, enum spa_direction direction) {
	struct pw_node *node = nullptr;
	helper->lock();
	if (helper->default_nodes) {
		auto *nodes = helper->default_nodes.custom();
		nodes->mutex.lock();
		wait_for_nodes_init(helper, nodes->mutex);
		std::string_view name;
		switch (direction) {
			case SPA_DIRECTION_INPUT: name = nodes->default_source; break;
			case SPA_DIRECTION_OUTPUT: name = nodes->default_sink; break;
		}
		node = find_node_by_name_locked(helper, name);
		nodes->mutex.unlock();
	}
	helper->unlock();
	return node;
}

struct pw_node *find_node_by_name(Helper *helper, char const *name) {
	helper->lock();
	struct pw_node *found = find_node_by_name_locked(helper, name);
	helper->unlock();
	return found;
}

void get_node_props(Helper *helper, struct pw_node *proxy, std::vector<std::pair<std::string_view, std::string*> > const& props) {
	Node *node = ProxyPtr<Node>(proxy).custom();
	node->get_or_wait_for_info(nullptr, nullptr, props);
}

void lock_loop(Helper *helper) {
	pw_thread_loop_lock(helper->thread_loop);
}

void unlock_loop(Helper *helper) {
	pw_thread_loop_unlock(helper->thread_loop);
}

// C API

extern "C" {

struct user_pw_helper *user_pw_create_helper(int argc, char **argv, struct pw_helper_init_args const *conf) {
	return reinterpret_cast<struct user_pw_helper *>(create_helper(argc, argv, conf));
}

void user_pw_destroy_helper(struct user_pw_helper *helper) {
	destroy_helper(reinterpret_cast<Helper *>(helper));
}

struct pw_node *user_pw_get_default_node(struct user_pw_helper *helper, enum spa_direction direction) {
	return get_default_node(reinterpret_cast<Helper *>(helper), direction);
}

struct pw_node *user_pw_find_node_by_name(struct user_pw_helper *helper, char const *name) {
	return find_node_by_name(reinterpret_cast<Helper *>(helper), name);
}

void user_pw_lock_loop(struct user_pw_helper *helper) {
	lock_loop(reinterpret_cast<Helper *>(helper));
}

void user_pw_unlock_loop(struct user_pw_helper *helper) {
	unlock_loop(reinterpret_cast<Helper *>(helper));
}

// Add C wrapper for enumerate_pipewire_endpoints for GUI library
struct pw_node **user_pw_enumerate_endpoints(struct user_pw_helper *helper, int *count) {
	if (!helper || !count) {
		if (count) *count = 0;
		return nullptr;
	}
	
	std::vector<struct pw_node *> nodes = enumerate_pipewire_endpoints(reinterpret_cast<Helper *>(helper));
	*count = static_cast<int>(nodes.size());
	
	if (nodes.empty()) {
		return nullptr;
	}
	
	// Allocate C-style array for the GUI library
	struct pw_node **result = static_cast<struct pw_node **>(malloc(nodes.size() * sizeof(struct pw_node *)));
	if (!result) {
		*count = 0;
		return nullptr;
	}
	
	for (size_t i = 0; i < nodes.size(); ++i) {
		result[i] = nodes[i];
	}
	
	return result;
}

void user_pw_free_endpoints(struct pw_node **endpoints) {
	if (endpoints) {
		free(endpoints);
	}
}

// Helper functions for configuration parsing
static std::string trim(const std::string &s) {
	auto start = s.begin();
	while (start != s.end() && std::isspace(*start)) ++start;
	auto end = s.end();
	do { --end; } while (std::distance(start, end) > 0 && std::isspace(*end));
	return std::string(start, end + 1);
}

static bool parse_bool(const std::string &val, bool def) {
	if (val == "true" || val == "1" || val == "yes" || val == "on") return true;
	if (val == "false" || val == "0" || val == "no" || val == "off") return false;
	return def;
}

// Note: Configuration validation helpers are implemented in main.c to avoid duplication

// -----------------------------------------------------------------------------
// Environment variable integration (Task 2.2)
// -----------------------------------------------------------------------------

static uint32_t env_to_uint(const char *val, uint32_t def_val) {
	if (!val || !*val) return def_val;
	char *end = nullptr;
	unsigned long v = std::strtoul(val, &end, 10);
	if (end && *end) return def_val; // invalid numeric
	return (uint32_t)v;
}

static bool env_to_bool(const char *val, bool def_val) {
	if (!val || !*val) return def_val;
	std::string s(val);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return parse_bool(s, def_val);
}

} // extern "C"

extern "C" {

void pw_asio_apply_env_overrides(struct pw_helper_init_args *args) {
	if (!args) return;

	const char *v;

	v = std::getenv("PIPEWIREASIO_SAMPLE_RATE");
	args->sample_rate = env_to_uint(v, args->sample_rate);

	v = std::getenv("PIPEWIREASIO_BUFFER_SIZE");
	args->buffer_size = env_to_uint(v, args->buffer_size);

	v = std::getenv("PIPEWIREASIO_INPUT_CHANNELS");
	args->num_input_channels = env_to_uint(v, args->num_input_channels);

	v = std::getenv("PIPEWIREASIO_OUTPUT_CHANNELS");
	args->num_output_channels = env_to_uint(v, args->num_output_channels);

	v = std::getenv("PIPEWIREASIO_AUTO_CONNECT");
	args->auto_connect = env_to_bool(v, args->auto_connect);

	v = std::getenv("PIPEWIREASIO_RT_PRIORITY");
	args->rt_priority = static_cast<int>(env_to_uint(v, static_cast<uint32_t>(args->rt_priority)));

	v = std::getenv("PIPEWIREASIO_EXCLUSIVE_MODE");
	args->exclusive_mode = env_to_bool(v, args->exclusive_mode);

	// String valued env vars need to persist
	static std::string in_dev, out_dev, client_name;

	v = std::getenv("PIPEWIREASIO_INPUT_DEVICE");
	if (v && *v) { in_dev = v; args->input_device_name = in_dev.c_str(); }

	v = std::getenv("PIPEWIREASIO_OUTPUT_DEVICE");
	if (v && *v) { out_dev = v; args->output_device_name = out_dev.c_str(); }

	v = std::getenv("PIPEWIREASIO_CLIENT_NAME");
	if (v && *v) { client_name = v; args->client_name = client_name.c_str(); }
}

int pw_asio_load_config(struct pw_helper_init_args *args, const char *config_path) {
	if (!args || !config_path) return -1;
	pw_asio_init_default_config(args);
	std::ifstream f(config_path);
	if (!f.is_open()) return -2;
	std::string line, section;
	while (std::getline(f, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#') continue;
		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			std::transform(section.begin(), section.end(), section.begin(), ::tolower);
			continue;
		}
		auto eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = trim(line.substr(0, eq));
		std::string val = trim(line.substr(eq + 1));
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);
		if (section == "audio") {
			if (key == "sample_rate") args->sample_rate = std::stoi(val);
			else if (key == "buffer_size") args->buffer_size = std::stoi(val);
			else if (key == "input_channels") args->num_input_channels = std::stoi(val);
			else if (key == "output_channels") args->num_output_channels = std::stoi(val);
		} else if (section == "devices") {
			if (key == "input_device") {
				static std::string in_dev; in_dev = val; args->input_device_name = in_dev.c_str();
			} else if (key == "output_device") {
				static std::string out_dev; out_dev = val; args->output_device_name = out_dev.c_str();
			} else if (key == "auto_connect") args->auto_connect = parse_bool(val, true);
		} else if (section == "performance") {
			if (key == "rt_priority") args->rt_priority = std::stoi(val);
			else if (key == "exclusive_mode") args->exclusive_mode = parse_bool(val, false);
		} else if (section == "advanced") {
			if (key == "client_name") {
				static std::string cname; cname = val; args->client_name = cname.c_str();
			} else if (key == "debug_logging") {
				// Not used in args, but could be handled here if needed
			}
		}
	}

	// Apply environment variable overrides (env takes precedence)
	pw_asio_apply_env_overrides(args);

	if (!pw_asio_is_valid_sample_rate(args->sample_rate)) {
		return -PW_ASIO_ERROR_INVALID_PARAMETER;
	}
	if (!pw_asio_is_valid_buffer_size(args->buffer_size)) {
		return -PW_ASIO_ERROR_BUFFER_SIZE_INVALID;
	}
	if (args->num_input_channels == 0 && args->num_output_channels == 0) {
		return -PW_ASIO_ERROR_INVALID_PARAMETER;
	}

	return 0;
}

int pw_asio_save_config(const struct pw_helper_init_args *args, const char *config_path) {
	if (!args || !config_path) return -1;
	
	std::ofstream f(config_path);
	if (!f.is_open()) return -2;
	
	f << "# PipeWireASIO Configuration File\n";
	f << "# This file contains settings for the PipeWireASIO driver\n\n";
	
	f << "[audio]\n";
	f << "sample_rate = " << args->sample_rate << "\n";
	f << "buffer_size = " << args->buffer_size << "\n";
	f << "input_channels = " << args->num_input_channels << "\n";
	f << "output_channels = " << args->num_output_channels << "\n\n";
	
	f << "[devices]\n";
	f << "input_device = " << (args->input_device_name ? args->input_device_name : "") << "\n";
	f << "output_device = " << (args->output_device_name ? args->output_device_name : "") << "\n";
	f << "auto_connect = " << (args->auto_connect ? "true" : "false") << "\n\n";
	
	f << "[performance]\n";
	f << "rt_priority = " << args->rt_priority << "\n";
	f << "exclusive_mode = " << (args->exclusive_mode ? "true" : "false") << "\n\n";
	
	f << "[advanced]\n";
	f << "client_name = " << (args->client_name ? args->client_name : "") << "\n";
	f << "debug_logging = false\n";
	
	return f.good() ? 0 : -3;
}

} // extern "C"

// -----------------------------------------------------------------------------
// Device hot-plug support (Task 3.1)
// -----------------------------------------------------------------------------

void set_device_callback(Helper *helper, DeviceCallback callback) {
	if (!helper) return;
	helper->lock();
	helper->device_cb = std::move(callback);
	helper->unlock();
}

} // namespace PwHelper

extern "C" {

typedef void (*user_pw_device_callback_t)(struct pw_node *node, int added, void *userdata);

void user_pw_set_device_callback(struct user_pw_helper *helper,
                                 user_pw_device_callback_t cb,
                                 void *userdata) {
    PwHelper::Helper *h = reinterpret_cast<PwHelper::Helper *>(helper);
    if (!h) return;

    if (!cb) {
        PwHelper::set_device_callback(h, nullptr);
        return;
    }

    PwHelper::set_device_callback(h, [cb, userdata](struct pw_node *node, bool added) {
        cb(node, added ? 1 : 0, userdata);
    });
}

int user_pw_schedule_work(struct user_pw_helper *helper, enum pw_op_type operation, void *userdata) {
    if (!helper) {
        return -1;
    }
    
    PwHelper::Helper *h = reinterpret_cast<PwHelper::Helper*>(helper);
    
    // Lock the worker mutex to schedule the operation
    std::unique_lock<std::mutex> lock(h->worker_mutex);
    
    // Check if another operation is already pending
    if (h->pending_operation != PW_OP_NONE) {
        return -2; // Operation already in progress
    }
    
    // Schedule the operation
    h->pending_operation = operation;
    h->operation_userdata = userdata;
    
    // We need to unlock our mutex first to avoid deadlocks
    lock.unlock();
    
    // Process the operation - the worker callback will handle proper PipeWire thread locking
    h->process_pending_operations();
    
    return 0;
}

void user_pw_set_worker_callback(pw_worker_callback_t callback) {
	g_worker_callback = callback;
}

// Add C API functions for event processing
void user_pw_trigger_event_processing(struct user_pw_helper *helper) {
	PwHelper::Helper *h = reinterpret_cast<PwHelper::Helper *>(helper);
	h->trigger_event_processing();
}

int user_pw_wait_for_filter_state(struct user_pw_helper *helper, struct pw_filter *filter, enum pw_filter_state target_state, int timeout_ms) {
	PwHelper::Helper *h = reinterpret_cast<PwHelper::Helper *>(helper);
	return h->wait_for_filter_state_transition(filter, target_state, timeout_ms) ? 1 : 0;
}

} // extern "C"
