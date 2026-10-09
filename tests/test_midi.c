/**
 * @file test_midi.c
 * @brief MIDI API tests for PipeWine
 */

#include "test_common.h"
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <string.h>

#include "pw_asio_midi.h"

#include "asio_test_api.h"

static int create_driver(IASIO **out_driver) {
    HRESULT hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUnknown, (void **)out_driver);
    if (FAILED(hr)) {
        printf("Driver creation failed: 0x%08x\n", (unsigned int)hr);
        return 0;
    }
    return *out_driver != NULL;
}

static int init_driver(IASIO **out_driver) {
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        return 0;
    }

    if (!create_driver(out_driver)) {
        CoUninitialize();
        return 0;
    }

    if (!(*out_driver)->lpVtbl->Init(*out_driver, NULL)) {
        (*out_driver)->lpVtbl->Release(*out_driver);
        *out_driver = NULL;
        CoUninitialize();
        return 0;
    }

    return 1;
}

static void shutdown_driver(IASIO *driver) {
    if (driver) {
        driver->lpVtbl->Release(driver);
    }
    CoUninitialize();
}

static void test_midi_info(void) {
    TEST_BEGIN("MIDI Info");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for MIDI info");
    if (!driver) {
        return;
    }

    PWASIO_MidiInfo info = {0};
    ASIOError err = driver->lpVtbl->Future(driver, PW_ASIO_MIDI_SELECTOR_GET_INFO, &info);
    TEST_ASSERT_EQ(ASE_SUCCESS, err, "MIDI info selector returns success");
    TEST_ASSERT_EQ(PW_ASIO_MIDI_API_VERSION, (int)info.api_version, "MIDI API version matches");

    shutdown_driver(driver);
}

static void test_midi_input_empty(void) {
    TEST_BEGIN("MIDI Input Empty");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for MIDI input");
    if (!driver) {
        return;
    }

    PWASIO_MidiInfo info = {0};
    ASIOError err = driver->lpVtbl->Future(driver, PW_ASIO_MIDI_SELECTOR_GET_INFO, &info);
    TEST_ASSERT_EQ(ASE_SUCCESS, err, "MIDI info selector returns success");

    if (info.num_inputs == 0) {
        printf("SKIP: No MIDI input ports configured\n");
        shutdown_driver(driver);
        return;
    }

    PWASIO_MidiEvent events[8] = {0};
    PWASIO_MidiBuffer buffer = {
        .capacity = 8,
        .count = 0,
        .events = events
    };

    err = driver->lpVtbl->Future(driver, PW_ASIO_MIDI_SELECTOR_GET_INPUT, &buffer);
    TEST_ASSERT_EQ(ASE_SUCCESS, err, "MIDI input selector returns success");
    TEST_ASSERT_EQ(0, (int)buffer.count, "MIDI input buffer starts empty");

    shutdown_driver(driver);
}

static void test_midi_output_queue(void) {
    TEST_BEGIN("MIDI Output Queue");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for MIDI output");
    if (!driver) {
        return;
    }

    PWASIO_MidiInfo info = {0};
    ASIOError err = driver->lpVtbl->Future(driver, PW_ASIO_MIDI_SELECTOR_GET_INFO, &info);
    TEST_ASSERT_EQ(ASE_SUCCESS, err, "MIDI info selector returns success");

    if (info.num_outputs == 0) {
        printf("SKIP: No MIDI output ports configured\n");
        shutdown_driver(driver);
        return;
    }

    PWASIO_MidiEvent events[2] = {0};
    events[0].offset = 0;
    events[0].size = 3;
    events[0].port = 0;
    events[0].data[0] = 0x90;
    events[0].data[1] = 60;
    events[0].data[2] = 100;

    events[1].offset = 32;
    events[1].size = 3;
    events[1].port = 0;
    events[1].data[0] = 0x80;
    events[1].data[1] = 60;
    events[1].data[2] = 0;

    PWASIO_MidiBuffer buffer = {
        .capacity = 2,
        .count = 2,
        .events = events
    };

    err = driver->lpVtbl->Future(driver, PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT, &buffer);
    TEST_ASSERT_EQ(ASE_SUCCESS, err, "MIDI output selector returns success");
    TEST_ASSERT_EQ(2, (int)buffer.count, "MIDI output accepted events");

    shutdown_driver(driver);
}

int main(void) {
    printf("PipeWine MIDI Test Suite\n");
    printf("========================\n");

    test_midi_info();
    test_midi_input_empty();
    test_midi_output_queue();

    TEST_END();
}
