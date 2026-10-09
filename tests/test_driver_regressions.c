/* Deterministic driver regressions. Compile with PWASIO_LIVE_TESTS to exercise
 * the same driver lifecycle against a real PipeWire server, without registry
 * registration or installing the driver. The driver uses its actual ASIO ABI. */
#include <windows.h>
#include <pipewire/filter.h>
#include <pipewire/keys.h>
#include "../pw_helper_c.h"
#include "test_common.h"

static float input_dsp[4][8192], output_dsp[4][8192];

#ifndef PWASIO_LIVE_TESTS
static enum pw_filter_state mock_state = PW_FILTER_STATE_UNCONNECTED;
static int mock_connect_result;
static int alloc_until_failure = -1;
static bool missing_input;
static char requested_rate[32], requested_quantum[32];
static void *mock_heap_alloc(HANDLE heap, DWORD flags, SIZE_T size) {
    if (alloc_until_failure == 0) return NULL;
    if (alloc_until_failure > 0) --alloc_until_failure;
    return HeapAlloc(heap, flags, size);
}
static void *mock_dsp(void *port, uint32_t samples) { return missing_input ? NULL : port; }
static int mock_connect(struct pw_filter *filter, enum pw_filter_flags flags,
                        const struct spa_pod **params, uint32_t n) {
    TEST_ASSERT(flags & PW_FILTER_FLAG_INACTIVE, "Prepared filter starts inactive");
    if (mock_connect_result < 0) return mock_connect_result;
    mock_state = PW_FILTER_STATE_PAUSED;
    return 0;
}
static int mock_disconnect(struct pw_filter *filter) { mock_state = PW_FILTER_STATE_UNCONNECTED; return 0; }
static enum pw_filter_state mock_get_state(struct pw_filter *filter, const char **error) { return mock_state; }
static int mock_properties(struct pw_filter *filter, void *port, const struct spa_dict *dict) {
    snprintf(requested_rate, sizeof(requested_rate), "%s", spa_dict_lookup(dict, PW_KEY_NODE_RATE));
    snprintf(requested_quantum, sizeof(requested_quantum), "%s", spa_dict_lookup(dict, PW_KEY_NODE_FORCE_QUANTUM));
    return 0;
}
static int mock_wait(struct user_pw_helper *helper, struct pw_filter *filter, enum pw_filter_state state, int timeout) {
    return mock_state == state;
}
static void mock_lock(struct user_pw_helper *helper) {}
static void mock_destroy_filter(struct pw_filter *filter) {}
static void mock_destroy_helper(struct user_pw_helper *helper) {}
static void mock_connections(struct user_pw_helper *helper, uint32_t node, const char *source,
                             const char *sink, uint64_t inputs, uint64_t outputs) {}
static struct pw_buffer *mock_dequeue(void *port) {
    static struct pw_buffer buffers[8];
    static struct spa_buffer spa[8];
    static struct spa_data data[8];
    static struct spa_chunk chunks[8];
    for (int i = 0; i < 8; ++i) {
        void *audio = i < 4 ? (void *)input_dsp[i] : (void *)output_dsp[i - 4];
        if (port != audio) continue;
        if (missing_input) return NULL;
        chunks[i].size = sizeof(input_dsp[0]);
        data[i] = (struct spa_data){.data = audio, .maxsize = sizeof(input_dsp[0]), .chunk = &chunks[i]};
        spa[i] = (struct spa_buffer){.n_datas = 1, .datas = &data[i]};
        buffers[i].buffer = &spa[i];
        return &buffers[i];
    }
    return port;
}
static int mock_queue(void *port, struct pw_buffer *buffer) { return 0; }
static int mock_active(struct pw_filter *filter, bool active) { return 0; }
static struct pw_loop *mock_data_loop(struct pw_filter *filter) { return NULL; }
#define HeapAlloc mock_heap_alloc
#define pw_filter_get_dsp_buffer mock_dsp
#define pw_filter_dequeue_buffer mock_dequeue
#define pw_filter_queue_buffer mock_queue
#define pw_filter_connect mock_connect
#define pw_filter_disconnect mock_disconnect
#define pw_filter_get_state mock_get_state
#define pw_filter_update_properties mock_properties
#define pw_filter_set_active mock_active
#define pw_filter_get_data_loop mock_data_loop
#define pw_filter_destroy mock_destroy_filter
#define user_pw_wait_for_filter_state mock_wait
#define user_pw_lock_loop mock_lock
#define user_pw_unlock_loop mock_lock
#define user_pw_destroy_helper mock_destroy_helper
#define user_pw_configure_audio_connections mock_connections
#endif

#include "../asio.c"
#include "../main.c"

static IWineASIOImpl *callback_driver;
static volatile LONG callback_count;
static volatile LONG callback_entered;
static volatile LONG slow_callback;
static DWORD callback_thread_id;

static LONG requested_reset;
static bool support_time_info;
static volatile LONG live_input_seen, live_output_seen;
static LONG ASIO_CALLBACK host_message(LONG selector, LONG value, void *message, double *opt) {
    if (selector == kAsioResetRequest) ++requested_reset;
    return selector == kAsioSupportsTimeInfo && support_time_info;
}
static void ASIO_CALLBACK host_callback(LONG index, ASIOBool direct) {
    callback_thread_id = GetCurrentThreadId();
    InterlockedExchange(&callback_entered, 1);
    if (callback_driver->input_channel[3].active &&
        ((float *)callback_driver->input_channel[3].wine_buffers[index])[0] == 0.75f)
        InterlockedExchange(&live_input_seen, 1);
    if (InterlockedCompareExchange(&slow_callback, 0, 0)) Sleep(150);
    for (int c = 0; c < callback_driver->wineasio_number_outputs; ++c) {
        IOChannel *channel = &callback_driver->output_channel[c];
        if (channel->active)
            for (int i = 0; i < callback_driver->asio_current_buffersize; ++i)
                ((float *)channel->wine_buffers[index])[i] = 0.25f;
    }
    InterlockedIncrement(&callback_count);
}
static ASIOTime * ASIO_CALLBACK host_time_callback(ASIOTime *time, LONG index, ASIOBool direct) {
    ASIOSamples position;
    ASIOTimeStamp timestamp;
    GetSamplePosition((LPWINEASIO)callback_driver, &position, &timestamp);
    TEST_ASSERT_EQ(time->timeInfo.samplePosition.lo, position.lo, "Time-info position agrees with GetSamplePosition");
    TEST_ASSERT_EQ(time->timeInfo.systemTime.lo, timestamp.lo, "Time-info timestamp agrees with GetSamplePosition");
    TEST_ASSERT_EQ(48000, time->timeInfo.sampleRate, "Time-info reports the stream rate");
    host_callback(index, direct);
    return time;
}
static ASIOCallbacks callbacks = { .bufferSwitch = host_callback, .asioMessage = host_message };

static IWineASIOImpl *new_driver(void) {
    IClassFactory *factory = NULL;
    IWineASIOImpl *driver = NULL;
    TEST_ASSERT_EQ(S_OK, DllGetClassObject(&CLSID_PipeWine, &IID_IClassFactory, (void **)&factory), "Factory created");
    if (!factory) return NULL;
    TEST_ASSERT_EQ(S_OK, factory->lpVtbl->CreateInstance(factory, NULL, &IID_IUnknown, (void **)&driver), "Driver created as IUnknown");
    IUnknown *unknown = NULL;
    TEST_ASSERT_EQ(S_OK, factory->lpVtbl->QueryInterface(factory, &IID_IUnknown, (void **)&unknown), "Factory supports IUnknown");
    if (unknown) unknown->lpVtbl->Release(unknown);
    factory->lpVtbl->Release(factory);
    if (!driver) return NULL;
#ifdef PWASIO_LIVE_TESTS
    TEST_ASSERT_EQ(ASIOTrue, Init((LPWINEASIO)driver, NULL), "Real PipeWire initialization");
    if (driver->asio_driver_state != Initialized) { Release((LPWINEASIO)driver); return NULL; }
#else
    driver->wineasio_number_inputs = driver->wineasio_number_outputs = 4;
    driver->wineasio_number_midi_inputs = driver->wineasio_number_midi_outputs = 0;
    driver->input_channel = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8 * sizeof(IOChannel));
    driver->output_channel = driver->input_channel + 4;
    for (int i = 0; i < 4; ++i) {
        driver->input_channel[i].port = input_dsp[i];
        driver->output_channel[i].port = output_dsp[i];
    }
    driver->wineasio_connect_to_hardware = false;
    driver->pw_filter = (struct pw_filter *)1;
    driver->pw_helper = (struct user_pw_helper *)1;
    driver->asio_driver_state = Initialized;
#endif
    callback_driver = driver;
    return driver;
}

static void test_validation_and_lifecycle(void) {
    IWineASIOImpl *driver = new_driver();
    if (!driver) return;
    TEST_ASSERT_EQ(S_FALSE, DllCanUnloadNow(), "Wine builtin stays available across COM reuse");
    LPWINEASIO iface = (LPWINEASIO)driver;
    void *unknown = NULL;
    TEST_ASSERT_EQ(S_OK, QueryInterface(iface, &IID_IUnknown, &unknown), "Driver supports IUnknown");
    TEST_ASSERT_EQ(1, Release(unknown), "Non-final release retains driver");
    TEST_ASSERT_EQ(Initialized, driver->asio_driver_state, "Non-final release preserves initialized state");
    unknown = (void *)1;
    TEST_ASSERT_EQ(E_NOINTERFACE, QueryInterface(iface, &IID_IClassFactory, &unknown), "Unsupported interface rejected");
    TEST_ASSERT_NULL(unknown, "Failed QueryInterface clears output");
    TEST_ASSERT_EQ(ASE_InvalidParameter, GetChannelInfo(iface, NULL), "Null channel query rejected");
    TEST_ASSERT_EQ(ASE_NoClock, SetSampleRate(iface, 0), "Zero sample rate rejected");
    TEST_ASSERT_EQ(ASE_NoClock, SetSampleRate(iface, NAN), "NaN sample rate rejected");
    TEST_ASSERT_EQ(ASE_NoClock, CanSampleRate(iface, INFINITY), "Infinite sample rate rejected");
    TEST_ASSERT_EQ(ASE_NoClock, CanSampleRate(iface, 48000.5), "Fractional sample rate rejected");
    TEST_ASSERT_EQ(ASE_OK, SetSampleRate(iface, 48000), "Valid sample rate accepted");
    ASIOBufferInfo buffers[2] = {{ .isInput = ASIOTrue, .channelNum = 3 }, { .isInput = ASIOFalse, .channelNum = 2 }};
    TEST_ASSERT_EQ(ASE_InvalidParameter, CreateBuffers(iface, buffers, -1, 128, &callbacks), "Negative channel count rejected");
    buffers[0].channelNum = -1;
    TEST_ASSERT_EQ(ASE_InvalidParameter, CreateBuffers(iface, buffers, 2, 128, &callbacks), "Negative channel index rejected");
    buffers[0].channelNum = driver->wineasio_number_inputs;
    TEST_ASSERT_EQ(ASE_InvalidParameter, CreateBuffers(iface, buffers, 2, 128, &callbacks), "Out of bounds channel index rejected");
    buffers[0].channelNum = 3;
    ASIOBufferInfo duplicate[2] = {buffers[0], buffers[0]};
    TEST_ASSERT_EQ(ASE_InvalidParameter, CreateBuffers(iface, duplicate, 2, 128, &callbacks), "Duplicate channels rejected");
    TEST_ASSERT_EQ(ASE_InvalidMode, CreateBuffers(iface, buffers, 2, 127, &callbacks), "Unsupported buffer size rejected");
    driver->wineasio_fixed_buffersize = true;
    TEST_ASSERT_EQ(ASE_InvalidMode, CreateBuffers(iface, buffers, 2, driver->asio_current_buffersize == 128 ? 256 : 128, &callbacks), "Fixed buffer mismatch rejected");
    driver->wineasio_fixed_buffersize = false;
#ifndef PWASIO_LIVE_TESTS
    alloc_until_failure = 2;
    TEST_ASSERT_EQ(ASE_NoMemory, CreateBuffers(iface, buffers, 2, 128, &callbacks), "Partial allocation failure reported");
    alloc_until_failure = -1;
    TEST_ASSERT_NULL(driver->input_channel[3].wine_buffers[0], "Earlier allocations freed on failure");
    TEST_ASSERT_NULL(buffers[0].buffers[0], "Failed request exposes no stale buffer");
    mock_connect_result = -EIO;
    TEST_ASSERT_EQ(ASE_HWMalfunction, CreateBuffers(iface, buffers, 2, 128, &callbacks), "Connection failure reported");
    mock_connect_result = 0;
    TEST_ASSERT_NULL(driver->output_channel[2].wine_buffers[0], "Connection failure releases buffers");
#endif
    ASIOError result = CreateBuffers(iface, buffers, 2, 128, &callbacks);
    TEST_ASSERT_EQ(ASE_OK, result, "Sparse input/output buffer creation succeeds");
    if (result != ASE_OK) { Release(iface); return; }
    TEST_ASSERT_EQ(128, driver->asio_current_buffersize, "Host requested size is honored");
    TEST_ASSERT_EQ(ASE_InvalidMode, SetSampleRate(iface, 96000), "Prepared stream rejects clock mutation");
    struct pwasio_gui_conf conf = { .user = driver, .cf_buffer_size = 512, .cf_sample_rate = 96000,
                                   .cf_input_channels = 64, .cf_output_channels = 64 };
    GuiApplyConfig(&conf);
    TEST_ASSERT_EQ(128, driver->asio_current_buffersize, "GUI preserves live buffer size");
    TEST_ASSERT(driver->wineasio_number_inputs < 64, "GUI preserves allocated channel count");
    TEST_ASSERT_EQ(48000, driver->asio_sample_rate, "GUI preserves stream clock");
    TEST_ASSERT_EQ(1, requested_reset, "GUI requests host reset");
#ifndef PWASIO_LIVE_TESTS
    TEST_ASSERT_STR_EQ("1/48000", requested_rate, "PipeWire rate uses inverse sample rate");
    TEST_ASSERT_STR_EQ("128", requested_quantum, "Node requests host quantum");
#endif
    TEST_ASSERT_EQ(ASE_OK, DisposeBuffers(iface), "Buffer disposal succeeds");
    TEST_ASSERT_NOT_NULL(driver->pw_filter, "Disposal preserves reusable filter");
    TEST_ASSERT_NOT_NULL(driver->input_channel[3].port, "Disposal preserves ports");
    TEST_ASSERT_EQ(ASE_OK, SetSampleRate(iface, 96000), "Clock can change after disposal");
    TEST_ASSERT_EQ(ASE_OK, CreateBuffers(iface, buffers, 2, 256, &callbacks), "Buffers recreated at a new size and rate");
    TEST_ASSERT_EQ(ASE_OK, DisposeBuffers(iface), "Recreated buffers disposed");
    Release(iface);
}

#ifndef PWASIO_LIVE_TESTS
static void test_time_info(void) {
    IWineASIOImpl *driver = new_driver();
    if (!driver) return;
    LPWINEASIO iface = (LPWINEASIO)driver;
    ASIOBufferInfo buffer = {.isInput = ASIOFalse, .channelNum = 2};
    ASIOCallbacks time_callbacks = {.bufferSwitchTimeInfo = host_time_callback, .asioMessage = host_message};
    support_time_info = true;
    SetSampleRate(iface, 48000);
    TEST_ASSERT_EQ(ASE_OK, CreateBuffers(iface, &buffer, 1, 128, &time_callbacks), "Time-info-only host can prepare buffers");
    Start(iface);
    struct spa_io_position position = {.clock = {.duration = 128, .rate = {1, 48000}, .nsec = 1000000}};
    pipewire_process_callback(driver, &position);
    position.clock.nsec += 2666666;
    pipewire_process_callback(driver, &position);
    TEST_ASSERT_EQ(ASE_NotPresent, Future(iface, kAsioCanTimeCode, NULL), "Unsupported time code is not advertised");
    Stop(iface);
    DisposeBuffers(iface);
    Release(iface);
    support_time_info = false;
}

static void test_midi_bounds(void) {
    IWineASIOImpl *driver = new_driver();
    if (!driver) return;
    driver->wineasio_number_midi_inputs = driver->wineasio_number_midi_outputs = 1;
    driver->midi_input_ports = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2 * sizeof(MidiPort));
    driver->midi_output_ports = driver->midi_input_ports + 1;
    uint64_t storage[16] = {0};
    struct spa_chunk chunk = {0};
    struct spa_data data = {.data = storage, .maxsize = sizeof(storage), .chunk = &chunk};
    struct spa_buffer spa = {.n_datas = 1, .datas = &data};
    struct pw_buffer buffer = {.buffer = &spa};
    driver->midi_input_ports[0].active = driver->midi_output_ports[0].active = true;
    driver->midi_input_ports[0].port = driver->midi_output_ports[0].port = &buffer;
    PWASIO_MidiEvent event = {.size = 3, .data = {0x90, 60, 100}, .offset = 300};
    PWASIO_MidiBuffer events = {.events = &event, .count = 2, .capacity = 1};
    TEST_ASSERT_EQ(ASE_InvalidParameter, Future((LPWINEASIO)driver, PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT, &events), "MIDI count exceeding capacity rejected");
    events.count = 1;
    TEST_ASSERT_EQ(ASE_SUCCESS, Future((LPWINEASIO)driver, PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT, &events), "Short MIDI event accepted");
    process_midi_output(driver, 128);
    TEST_ASSERT_EQ(40, chunk.size, "MIDI chunk length describes the actual sequence");
    TEST_ASSERT_EQ(1, chunk.stride, "MIDI chunk uses byte stride");
    process_midi_input(driver, 128);
    struct midi_event received;
    TEST_ASSERT(midi_buffer_read(&driver->midi_in_buffer, &received), "MIDI output sequence can be read back");
    TEST_ASSERT_EQ(127, received.offset, "MIDI offset clamps to quantum");
    TEST_ASSERT_EQ(3, received.size, "MIDI payload size preserved");
    events.count = 1;
    Future((LPWINEASIO)driver, PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT, &events);
    data.maxsize = sizeof(struct spa_pod_sequence);
    process_midi_output(driver, 128);
    TEST_ASSERT_EQ(sizeof(struct spa_pod_sequence), chunk.size, "Small MIDI buffer retains a valid empty sequence");
    ((struct spa_pod *)storage)->size = UINT32_MAX;
    process_midi_input(driver, 128);
    TEST_ASSERT_EQ(0, midi_buffer_count(&driver->midi_in_buffer), "Malformed MIDI sequence rejected before iterating");
    Release((LPWINEASIO)driver);
}

static void test_audio_process(void) {
    IWineASIOImpl *driver = new_driver();
    if (!driver) return;
    ASIOBufferInfo buffers[2] = {{.isInput = ASIOTrue, .channelNum = 3}, {.isInput = ASIOFalse, .channelNum = 2}};
    LPWINEASIO iface = (LPWINEASIO)driver;
    SetSampleRate(iface, 48000);
    TEST_ASSERT_EQ(ASE_OK, CreateBuffers(iface, buffers, 2, 128, &callbacks), "Audio test buffers prepared");
    Start(iface);
    for (int i = 0; i < 128; ++i) input_dsp[3][i] = 0.5f;
    memset(output_dsp, 0x55, sizeof(output_dsp));
    struct spa_io_position position = { .clock = { .duration = 128, .rate = {1, 48000}, .nsec = 123000 } };
    LONG before = callback_count;
    pipewire_process_callback(driver, &position);
    TEST_ASSERT_EQ(before + 1, callback_count, "Exactly one host callback per quantum");
    TEST_ASSERT(((float *)buffers[0].buffers[0])[127] == 0.5f, "Sparse input channel reaches host");
    TEST_ASSERT(output_dsp[2][127] == 0.25f, "Sparse output channel reaches PipeWire");
    TEST_ASSERT(output_dsp[0][127] == 0, "Inactive output contains silence");
    TEST_ASSERT_EQ(128, driver->asio_sample_position, "Sample position advances by requested frames");
    missing_input = true;
    pipewire_process_callback(driver, &position);
    missing_input = false;
    TEST_ASSERT(((float *)buffers[0].buffers[1])[127] == 0, "Unavailable input is cleared rather than replayed");
    before = callback_count;
    position.clock.duration = 256;
    pipewire_process_callback(driver, &position);
    TEST_ASSERT_EQ(before, callback_count, "Quantum mismatch does not advance ASIO");
    TEST_ASSERT(output_dsp[2][255] == 0, "Quantum mismatch produces silence");
    position.clock.duration = 128;
    position.clock.rate.denom = 96000;
    pipewire_process_callback(driver, &position);
    TEST_ASSERT_EQ(before, callback_count, "Rate mismatch does not advance ASIO");
    position.clock.duration = 16384;
    pipewire_process_callback(driver, &position);
    TEST_ASSERT_EQ(before, callback_count, "Oversized graph quantum cannot overrun negotiated DSP buffers");
    TEST_ASSERT_EQ(ASE_OK, Stop(iface), "Stop succeeds");
    DisposeBuffers(iface);
    Release(iface);
}
#else
struct virtual_endpoint { struct pw_filter *filter; void *port; bool capture; struct spa_hook listener; };
static void virtual_process(void *data, struct spa_io_position *position) {
    struct virtual_endpoint *endpoint = data;
    float *buffer = pw_filter_get_dsp_buffer(endpoint->port, position->clock.duration);
    if (!buffer) return;
    if (endpoint->capture) {
        for (uint32_t i = 0; i < position->clock.duration; ++i) buffer[i] = 0.75f;
    } else if (buffer[0] == 0.25f) InterlockedExchange(&live_output_seen, 1);
}
static const struct pw_filter_events virtual_events = {
    .version = PW_VERSION_FILTER_EVENTS, .process = virtual_process,
};
static bool create_endpoint(IWineASIOImpl *driver, struct virtual_endpoint *endpoint, bool capture) {
    endpoint->capture = capture;
    user_pw_lock_loop(driver->pw_helper);
    endpoint->filter = pw_filter_new(driver->pw_core, capture ? "test-source" : "test-sink",
        pw_properties_new(PW_KEY_NODE_NAME, capture ? "test-source" : "test-sink",
                          PW_KEY_MEDIA_CLASS, capture ? "Audio/Source" : "Audio/Sink",
                          PW_KEY_NODE_AUTOCONNECT, "false", NULL));
    if (!endpoint->filter) { user_pw_unlock_loop(driver->pw_helper); return false; }
    pw_filter_add_listener(endpoint->filter, &endpoint->listener,
                           &virtual_events, endpoint);
    endpoint->port = pw_filter_add_port(endpoint->filter, capture ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT,
        PW_FILTER_PORT_FLAG_MAP_BUFFERS, 0,
        pw_properties_new(PW_KEY_PORT_NAME, capture ? "capture_0" : "playback_0",
                          PW_KEY_FORMAT_DSP, JACK_DEFAULT_AUDIO_TYPE, NULL), NULL, 0);
    int result = pw_filter_connect(endpoint->filter, PW_FILTER_FLAG_RT_PROCESS | PW_FILTER_FLAG_INACTIVE, NULL, 0);
    user_pw_unlock_loop(driver->pw_helper);
    if (result < 0 || !user_pw_wait_for_filter_state(driver->pw_helper, endpoint->filter, PW_FILTER_STATE_PAUSED, 5000)) return false;
    user_pw_lock_loop(driver->pw_helper);
    pw_filter_set_active(endpoint->filter, true);
    user_pw_unlock_loop(driver->pw_helper);
    return true;
}
static void destroy_endpoint(IWineASIOImpl *driver, struct virtual_endpoint *endpoint) {
    user_pw_lock_loop(driver->pw_helper);
    pw_filter_destroy(endpoint->filter);
    endpoint->filter = NULL;
    user_pw_unlock_loop(driver->pw_helper);
}

static void test_live_callbacks(void) {
    IWineASIOImpl *driver = new_driver();
    if (!driver) return;
    LPWINEASIO iface = (LPWINEASIO)driver;
    ASIOBufferInfo buffers[2] = {{.isInput = ASIOTrue, .channelNum = 3}, {.isInput = ASIOFalse, .channelNum = 2}};
    struct virtual_endpoint source = {0}, sink = {0};
    TEST_ASSERT(create_endpoint(driver, &source, true), "Virtual source created");
    TEST_ASSERT(create_endpoint(driver, &sink, false), "Virtual sink created");
    driver->wineasio_connect_to_hardware = true;
    lstrcpyW(driver->pwasio_input_device_name, u"test-source");
    lstrcpyW(driver->pwasio_output_device_name, u"test-sink");
    SetSampleRate(iface, 48000);
    ASIOError result = CreateBuffers(iface, buffers, 2, 128, &callbacks);
    TEST_ASSERT_EQ(ASE_OK, result, "Live callback buffer creation");
    if (result != ASE_OK) { Release(iface); return; }
    user_pw_lock_loop(driver->pw_helper);
    const struct spa_dict_item props[] = {{PW_KEY_NODE_ALWAYS_PROCESS, "true"}, {PW_KEY_NODE_WANT_DRIVER, "true"}};
    const struct spa_dict dict = SPA_DICT_INIT(props, 2);
    pw_filter_update_properties(driver->pw_filter, NULL, &dict);
    user_pw_unlock_loop(driver->pw_helper);
    callback_count = 0;
    TEST_ASSERT_EQ(ASE_OK, Start(iface), "Live stream starts");
    for (int i = 0; i < 200 && !callback_count; ++i) Sleep(10);
    TEST_ASSERT(callback_count > 0, "PipeWire invokes host callbacks on Wine-created data thread");
    for (int i = 0; i < 200 && (!live_input_seen || !live_output_seen); ++i) Sleep(10);
    TEST_ASSERT(live_input_seen, "Selected source automatically connects to sparse ASIO input");
    TEST_ASSERT(live_output_seen, "Sparse ASIO output automatically connects to selected sink");
    live_output_seen = 0;
    destroy_endpoint(driver, &sink);
    TEST_ASSERT(create_endpoint(driver, &sink, false), "Selected sink reappears after hot unplug");
    for (int i = 0; i < 200 && !live_output_seen; ++i) Sleep(10);
    TEST_ASSERT(live_output_seen, "Device reconnects from registry events without polling");
    TEST_ASSERT(callback_thread_id != GetCurrentThreadId() && callback_thread_id != 0, "Callback has a valid distinct Wine thread ID");
    callback_entered = 0;
    slow_callback = 1;
    for (int i = 0; i < 100 && !callback_entered; ++i) Sleep(5);
    TEST_ASSERT_EQ(ASE_OK, Stop(iface), "Stop waits for in-flight slow callback");
    LONG stopped_count = callback_count;
    Sleep(200);
    TEST_ASSERT_EQ(stopped_count, callback_count, "No callback occurs after Stop returns");
    slow_callback = 0;
    TEST_ASSERT_EQ(ASE_OK, Start(iface), "Live stream restarts");
    for (int i = 0; i < 200 && callback_count == stopped_count; ++i) Sleep(10);
    TEST_ASSERT(callback_count > stopped_count, "Callbacks resume after restart");
    Stop(iface);
    DisposeBuffers(iface);
    destroy_endpoint(driver, &source);
    destroy_endpoint(driver, &sink);
    Release(iface);
}
#endif

int main(void) {
    test_validation_and_lifecycle();
#ifndef PWASIO_LIVE_TESTS
    test_audio_process();
    test_midi_bounds();
    test_time_info();
#else
    test_live_callbacks();
#endif
    TEST_END();
}
