#pragma once

#if !defined(_Nullable)
#if defined(__clang__)
#else
#define _Nullable
#endif
#endif

#include <spa/utils/defs.h>

#include <pthread.h>

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pw_helper_thread_creator_t)(
	pthread_t *out_thread,
	const pthread_attr_t *attrs,
	void *(*function)(void *arg),
	void *arg);

// Enhanced configuration structure
struct pw_helper_init_args {
	/// The application that uses the driver.
	char const *_Nullable app_name;
	/// Place to store created pw_loop.
	struct pw_loop **_Nullable loop;
	/// Place to store created pw_context.
	struct pw_context **_Nullable context;
	/// Place to store pw_core proxy.
	struct pw_core **_Nullable core;

	pw_helper_thread_creator_t _Nullable thread_creator;

	const char *client_name;
	const char *input_device_name;
	const char *output_device_name;
	uint32_t sample_rate;
	uint32_t buffer_size;
	uint32_t num_input_channels;
	uint32_t num_output_channels;
	bool auto_connect;
	bool exclusive_mode;
	int rt_priority;
	const char *config_file_path;
	
	// Debug logging configuration
	bool debug_logging;
	int log_level; // 0=Error, 1=Warning, 2=Info, 3=Debug, 4=Trace
};

// Default configuration values
#define PW_ASIO_DEFAULT_SAMPLE_RATE     44100
#define PW_ASIO_DEFAULT_BUFFER_SIZE     1024
#define PW_ASIO_DEFAULT_INPUT_CHANNELS  16
#define PW_ASIO_DEFAULT_OUTPUT_CHANNELS 16
#define PW_ASIO_MIN_BUFFER_SIZE         16
#define PW_ASIO_MAX_BUFFER_SIZE         8192
#define PW_ASIO_DEFAULT_LOG_LEVEL       1  // Warning level by default

// Logging levels
enum pw_asio_log_level {
    PW_ASIO_LOG_ERROR = 0,
    PW_ASIO_LOG_WARNING = 1,
    PW_ASIO_LOG_INFO = 2,
    PW_ASIO_LOG_DEBUG = 3,
    PW_ASIO_LOG_TRACE = 4
};

// Error codes
enum pw_asio_error {
    PW_ASIO_OK = 0,
    PW_ASIO_ERROR_INIT_FAILED,
    PW_ASIO_ERROR_DEVICE_NOT_FOUND,
    PW_ASIO_ERROR_FORMAT_NOT_SUPPORTED,
    PW_ASIO_ERROR_BUFFER_SIZE_INVALID,
    PW_ASIO_ERROR_CONNECTION_FAILED,
    PW_ASIO_ERROR_ALREADY_RUNNING,
    PW_ASIO_ERROR_NOT_RUNNING,
    PW_ASIO_ERROR_INVALID_PARAMETER
};

// Configuration management functions
int pw_asio_load_config(struct pw_helper_init_args *args, const char *config_path);
int pw_asio_save_config(const struct pw_helper_init_args *args, const char *config_path);
void pw_asio_init_default_config(struct pw_helper_init_args *args);

// Utility functions
const char *pw_asio_error_string(enum pw_asio_error error);
bool pw_asio_is_valid_buffer_size(uint32_t size);
bool pw_asio_is_valid_sample_rate(uint32_t rate);

// Environment variable overrides
void pw_asio_apply_env_overrides(struct pw_helper_init_args *args);

// Logging functions
void pw_asio_set_log_level(int level);
int pw_asio_get_log_level(void);
void pw_asio_log(int level, const char *function, int line, const char *format, ...);

// Logging macros
#define PWASIO_LOG_ERROR(format, ...) pw_asio_log(PW_ASIO_LOG_ERROR, __func__, __LINE__, format, ##__VA_ARGS__)
#define PWASIO_LOG_WARNING(format, ...) pw_asio_log(PW_ASIO_LOG_WARNING, __func__, __LINE__, format, ##__VA_ARGS__)
#define PWASIO_LOG_INFO(format, ...) pw_asio_log(PW_ASIO_LOG_INFO, __func__, __LINE__, format, ##__VA_ARGS__)
#define PWASIO_LOG_DEBUG(format, ...) pw_asio_log(PW_ASIO_LOG_DEBUG, __func__, __LINE__, format, ##__VA_ARGS__)
#define PWASIO_LOG_TRACE(format, ...) pw_asio_log(PW_ASIO_LOG_TRACE, __func__, __LINE__, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
