/**
 * @file test_dsp.c
 * @brief Consolidated DSP tests for PipeWine
 *
 * Consolidates:
 * - simple_dsp_test.c
 * - wine_dsp_test.c
 */

#include "test_common.h"
#include <signal.h>
#include <string.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/latency-utils.h>
#include <spa/pod/builder.h>

struct test_data {
    struct pw_main_loop *loop;
    struct pw_filter *filter;
    void *in_port;
    void *out_port;
    int state_changes;
};

static void on_process(void *userdata, struct spa_io_position *position) {
    struct test_data *data = (struct test_data *)userdata;
    uint32_t n_samples = position->clock.duration;

    float *in = (float *)pw_filter_get_dsp_buffer(data->in_port, n_samples);
    float *out = (float *)pw_filter_get_dsp_buffer(data->out_port, n_samples);

    if (in && out) {
        memcpy(out, in, n_samples * sizeof(float));
    }
}

static void on_state_changed(void *userdata, enum pw_filter_state old_state, enum pw_filter_state state, const char *error) {
    struct test_data *data = (struct test_data *)userdata;
    (void)old_state;

    data->state_changes++;

    if (state == PW_FILTER_STATE_ERROR) {
        printf("FAIL: Filter entered error state: %s\n", error ? error : "unknown");
        pw_main_loop_quit(data->loop);
        return;
    }

    if (state == PW_FILTER_STATE_PAUSED) {
        pw_main_loop_quit(data->loop);
    }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

static void do_quit(void *userdata, int signal_number) {
    (void)signal_number;
    struct test_data *data = (struct test_data *)userdata;
    pw_main_loop_quit(data->loop);
}

static void test_filter_creation(void) {
    TEST_BEGIN("DSP Filter Creation");

    struct test_data data = {0};
    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    int argc = 0;
    char **argv = NULL;
    pw_init(&argc, &argv);

    data.loop = pw_main_loop_new(NULL);
    TEST_ASSERT_NOT_NULL(data.loop, "Main loop created");
    if (!data.loop) {
        pw_deinit();
        return;
    }

    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

    data.filter = pw_filter_new_simple(
        pw_main_loop_get_loop(data.loop),
        "test-dsp-filter",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter",
            PW_KEY_MEDIA_ROLE, "DSP",
            NULL),
        &filter_events,
        &data);

    TEST_ASSERT_NOT_NULL(data.filter, "Filter created");
    if (!data.filter) {
        pw_main_loop_destroy(data.loop);
        pw_deinit();
        return;
    }

    data.in_port = pw_filter_add_port(data.filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(void *),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "input",
            NULL),
        NULL, 0);

    data.out_port = pw_filter_add_port(data.filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(void *),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "output",
            NULL),
        NULL, 0);

    TEST_ASSERT(data.in_port && data.out_port, "Ports created");
    if (!data.in_port || !data.out_port) {
        pw_filter_destroy(data.filter);
        pw_main_loop_destroy(data.loop);
        pw_deinit();
        return;
    }

    struct spa_process_latency_info latency_info = SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC);
    params[0] = spa_process_latency_build(&b,
        SPA_PARAM_ProcessLatency,
        &latency_info);

    TEST_ASSERT(pw_filter_connect(data.filter, PW_FILTER_FLAG_RT_PROCESS, params, 1) >= 0,
                "Filter connected");

    pw_main_loop_run(data.loop);

    TEST_ASSERT(data.state_changes > 0, "Filter emitted state changes");

    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();
}

int main(void) {
    printf("PipeWire DSP Test Suite\n");
    printf("=======================\n");

    test_filter_creation();

    TEST_END();
}
