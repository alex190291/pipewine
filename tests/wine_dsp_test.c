#include <stdio.h>
#include <signal.h>
#include <unistd.h>

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
    struct test_data *data = userdata;
    uint32_t n_samples = position->clock.duration;
    
    float *in = pw_filter_get_dsp_buffer(data->in_port, n_samples);
    float *out = pw_filter_get_dsp_buffer(data->out_port, n_samples);
    
    if (in && out) {
        memcpy(out, in, n_samples * sizeof(float));
    }
}

static void on_state_changed(void *userdata, enum pw_filter_state old, enum pw_filter_state state, const char *error) {
    struct test_data *data = userdata;
    
    printf("Wine DSP Test: Filter state changed from %s to %s\n", 
           pw_filter_state_as_string(old), pw_filter_state_as_string(state));
    
    data->state_changes++;
    
    if (error) {
        printf("Wine DSP Test: Error: %s\n", error);
    }
    
    if (state == PW_FILTER_STATE_PAUSED) {
        printf("Wine DSP Test: SUCCESS - Filter reached paused state!\n");
        pw_main_loop_quit(data->loop);
    } else if (state == PW_FILTER_STATE_ERROR) {
        printf("Wine DSP Test: FAILED - Filter entered error state\n");
        pw_main_loop_quit(data->loop);
    }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

static void do_quit(void *userdata, int signal_number) {
    struct test_data *data = userdata;
    pw_main_loop_quit(data->loop);
}

int main(int argc, char *argv[]) {
    struct test_data data = { 0 };
    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    
    printf("Wine DSP Test: Starting PipeWire DSP filter test under Wine\n");
    
    pw_init(&argc, &argv);
    
    data.loop = pw_main_loop_new(NULL);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
    pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);
    
    printf("Wine DSP Test: Creating DSP filter...\n");
    data.filter = pw_filter_new_simple(
        pw_main_loop_get_loop(data.loop),
        "wine-dsp-test",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Filter", 
            PW_KEY_MEDIA_ROLE, "DSP",
            NULL),
        &filter_events,
        &data);
    
    if (!data.filter) {
        printf("Wine DSP Test: FAILED - Could not create filter\n");
        return 1;
    }
    
    printf("Wine DSP Test: Adding ports...\n");
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
    
    if (!data.in_port || !data.out_port) {
        printf("Wine DSP Test: FAILED - Could not create ports\n");
        return 1;
    }
    
    params[0] = spa_process_latency_build(&b, SPA_PARAM_ProcessLatency,
        &SPA_PROCESS_LATENCY_INFO_INIT(.ns = 10 * SPA_NSEC_PER_MSEC));
    
    printf("Wine DSP Test: Connecting filter...\n");
    if (pw_filter_connect(data.filter, PW_FILTER_FLAG_RT_PROCESS, params, 1) < 0) {
        printf("Wine DSP Test: FAILED - Could not connect filter\n");
        return 1;
    }
    
    printf("Wine DSP Test: Running main loop (waiting for state changes)...\n");
    
    // Set a timeout
    alarm(15);
    
    pw_main_loop_run(data.loop);
    
    printf("Wine DSP Test: Main loop exited. State changes: %d\n", data.state_changes);
    
    if (data.state_changes == 0) {
        printf("Wine DSP Test: FAILED - No state changes detected\n");
    }
    
    pw_filter_destroy(data.filter);
    pw_main_loop_destroy(data.loop);
    pw_deinit();
    
    return 0;
} 