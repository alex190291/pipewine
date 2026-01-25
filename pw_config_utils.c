#include "pw_helper_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

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
    args->num_midi_inputs = 0;
    args->num_midi_outputs = 0;
    args->auto_connect = 1; // true
    args->exclusive_mode = 0; // false
    args->rt_priority = 10;
    args->config_file_path = NULL;
    args->debug_logging = false;
    args->log_level = PW_ASIO_DEFAULT_LOG_LEVEL;
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

// --- Configuration Path Resolution ---

char *pw_asio_get_config_path(void) {
    char *path = NULL;
    const char *home = getenv("HOME");

    // Try user config first (higher priority)
    if (home) {
        size_t len = strlen(home) + 40;
        path = (char *)malloc(len);
        if (path) {
            snprintf(path, len, "%s/.config/pipewine/pipewine.conf", home);
            FILE *f = fopen(path, "r");
            if (f) {
                fclose(f);
                return path;
            }
            free(path);
        }
    }

    // Try system config
    path = strdup("/etc/pipewine/pipewine.conf");
    if (path) {
        FILE *f = fopen(path, "r");
        if (f) {
            fclose(f);
            return path;
        }
        free(path);
    }

    return NULL;
}

// Helper function to create directory recursively
static int mkdir_recursive(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) return -1;

    char *p = path_copy;

    // Skip leading slash
    if (*p == '/') p++;

    while (*p) {
        // Find next slash
        while (*p && *p != '/') p++;

        if (*p) {
            *p = '\0';
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                free(path_copy);
                return -1;
            }
            *p = '/';
            p++;
        }
    }

    // Create the final directory
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }

    free(path_copy);
    return 0;
}

char *pw_asio_get_user_config_path(void) {
    const char *home = getenv("HOME");
    if (!home) return NULL;

    size_t len = strlen(home) + 40;
    char *dir_path = (char *)malloc(len);
    char *file_path = (char *)malloc(len);

    if (!dir_path || !file_path) {
        free(dir_path);
        free(file_path);
        return NULL;
    }

    snprintf(dir_path, len, "%s/.config/pipewine", home);
    snprintf(file_path, len, "%s/pipewine.conf", dir_path);

    // Create directory if it doesn't exist (using proper mkdir instead of system())
    if (mkdir_recursive(dir_path) != 0) {
        fprintf(stderr, "Warning: Failed to create config directory: %s\n", dir_path);
    }

    free(dir_path);
    return file_path;
}

// --- Logging Functions ---

static int g_log_level = PW_ASIO_DEFAULT_LOG_LEVEL;

void pw_asio_set_log_level(int level) {
    if (level >= PW_ASIO_LOG_ERROR && level <= PW_ASIO_LOG_TRACE) {
        g_log_level = level;
    }
}

int pw_asio_get_log_level(void) {
    return g_log_level;
}

void pw_asio_log(int level, const char *function, int line, const char *format, ...) {
    if (level > g_log_level) return;

    static const char *level_names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    const char *level_name = (level >= 0 && level <= 4) ? level_names[level] : "???";

    // Get timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    // Print header
    FILE *out = (level <= PW_ASIO_LOG_WARNING) ? stderr : stdout;
    fprintf(out, "[%s] [PWASIO/%s] %s:%d: ", time_buf, level_name, function, line);

    // Print message
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);

    // Ensure newline
    size_t fmt_len = strlen(format);
    if (fmt_len > 0 && format[fmt_len - 1] != '\n') {
        fputc('\n', out);
    }
}
