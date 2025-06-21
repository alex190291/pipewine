#include "pw_helper_common.h"
#include <string.h>

void pw_asio_init_default_config(struct pw_helper_init_args *args) {
    if (!args) return;
    
    // Initialize all fields to safe defaults
    args->app_name = NULL;
    args->loop = NULL;
    args->context = NULL;
    args->core = NULL;
    args->thread_creator = NULL;
    args->client_name = NULL;
    args->input_device_name = NULL;
    args->output_device_name = NULL;
    args->sample_rate = PW_ASIO_DEFAULT_SAMPLE_RATE;
    args->buffer_size = PW_ASIO_DEFAULT_BUFFER_SIZE;
    args->num_input_channels = PW_ASIO_DEFAULT_INPUT_CHANNELS;
    args->num_output_channels = PW_ASIO_DEFAULT_OUTPUT_CHANNELS;
    args->auto_connect = 1; // true
    args->exclusive_mode = 0; // false
    args->rt_priority = 10;
    args->config_file_path = NULL;
}

const char *pw_asio_error_string(enum pw_asio_error error) {
    switch (error) {
        case PW_ASIO_OK: return "Success";
        case PW_ASIO_ERROR_INIT_FAILED: return "Initialization failed";
        case PW_ASIO_ERROR_DEVICE_NOT_FOUND: return "Device not found";
        case PW_ASIO_ERROR_FORMAT_NOT_SUPPORTED: return "Format not supported";
        case PW_ASIO_ERROR_BUFFER_SIZE_INVALID: return "Invalid buffer size";
        case PW_ASIO_ERROR_CONNECTION_FAILED: return "Connection failed";
        case PW_ASIO_ERROR_ALREADY_RUNNING: return "Already running";
        case PW_ASIO_ERROR_NOT_RUNNING: return "Not running";
        case PW_ASIO_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        default: return "Unknown error";
    }
}

bool pw_asio_is_valid_buffer_size(uint32_t size) {
    return size >= PW_ASIO_MIN_BUFFER_SIZE && size <= PW_ASIO_MAX_BUFFER_SIZE && 
           (size & (size - 1)) == 0; // Check if power of 2
}

bool pw_asio_is_valid_sample_rate(uint32_t rate) {
    // Common sample rates
    return rate == 8000 || rate == 11025 || rate == 16000 || rate == 22050 ||
           rate == 32000 || rate == 44100 || rate == 48000 || rate == 88200 ||
           rate == 96000 || rate == 176400 || rate == 192000;
} 