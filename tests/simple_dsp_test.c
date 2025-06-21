#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

#include <spa/pod/builder.h>
#include <spa/param/latency-utils.h>
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>

struct data {
    struct pw_main_loop *loop;
    struct pw_filter *filter;
    void *in_port;
    void *out_port;
};

static void on_process(void *userdata, struct spa_io_position *position) {
    struct data *data = userdata;
    float *in, *out;
    uint32_t n_samples = position->clock.duration;

    printf("Processing %u samples\n", n_samples);

    in = pw_filter_get_dsp_buffer(data->in_port, n_samples);
    out = pw_filter_get_dsp_buffer(data->out_port, n_samples);

    if (in == NULL || out == NULL)
        return;

    memcpy(out, in, n_samples * sizeof(float));
}

static void on_state_changed(void *data, enum pw_filter_state old, enum pw_filter_state state, const char *error) {
    printf("Filter state changed from %s to %s", pw_filter_state_as_string(old), pw_filter_state_as_string(state));
    if (error) {
        printf(": ERROR %s\n", error);
    } else {
        printf("\n");
    }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

static void do_quit(void *userdata, int signal_number) {
    struct data *data = userdata;
    pw_main_loop_quit(data->loop);
}

int main(int argc, char *argv[]) {
    struct data data = { 0, };
    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    pw_init(&argc, &argv);

    data.loop = pw_main_loop_new(NULL);

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

    data.in_port = pw_filter_add_port(data.filter,
        PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(void*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "input",
            NULL),
        NULL, 0);

    data.out_port = pw_filter_add_port(data.filter,
        PW_DIRECTION_OUTPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS,
        sizeof(void*),
        pw_properties_new(
            PW_KEY_FORMAT_DSP, "32 bit float mono audio",
            PW_KEY_PORT_NAME, "output",
            NULL),
        NULL, 0);

    params[0] = spa_process_latency_build(&b,
        SPA_PARAM_ProcessLatency,
        &SPA_PROCESS_LATENCY_INFO_INIT(
            .ns = 10 * SPA_NSEC_PER_MSEC
        ));

    if (pw_filter_connect(data.filter,
            PW_FILTER_FLAG_RT_PROCESS,
            params, 1) < 0) {
        fprintf(stderr, "can't connect\n");
        return -1;
    }

    printf("DSP filter created and connected, running...\n");
    pw_main_loop_run(data.loop);

    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();

    return 0;
} 