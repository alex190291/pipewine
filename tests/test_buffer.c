/**
 * @file test_buffer.c
 * @brief Consolidated buffer tests for PipeWine
 *
 * Consolidates:
 * - asio_buffer_test.c
 * - buffer_alloc_test.c
 * - buffer_focus_test.c
 * - buffer_restart_test.c
 * - buffer_validation_test.c
 * - simple_buffer_test.c
 */

#include "test_common.h"
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <string.h>

#include "asio_test_api.h"

static void ASIO_CALLBACK bufferSwitch(LONG doubleBufferIndex, ASIOBool directProcess) {
    (void)doubleBufferIndex;
    (void)directProcess;
}

static void ASIO_CALLBACK sampleRateDidChange(double sRate) {
    (void)sRate;
}

static LONG ASIO_CALLBACK asioMessage(LONG selector, LONG value, void *message, double *opt) {
    (void)selector;
    (void)value;
    (void)message;
    (void)opt;
    return 0;
}

static ASIOTime * ASIO_CALLBACK bufferSwitchTimeInfo(ASIOTime *params, LONG doubleBufferIndex, ASIOBool directProcess) {
    (void)params;
    (void)doubleBufferIndex;
    (void)directProcess;
    return params;
}

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

static void test_buffer_creation(void) {
    TEST_BEGIN("Buffer Creation");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for buffer creation");
    if (!driver) {
        return;
    }

    LONG minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    ASIOError err = driver->lpVtbl->GetBufferSize(driver, &minSize, &maxSize, &preferredSize, &granularity);
    TEST_ASSERT_EQ(0, err, "GetBufferSize should succeed");

    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = 1;
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    bufferInfos[1].isInput = 0;
    bufferInfos[1].channelNum = 0;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;

    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };

    err = driver->lpVtbl->CreateBuffers(driver, bufferInfos, 2, preferredSize, &callbacks);
    TEST_ASSERT_EQ(0, err, "CreateBuffers should succeed");
    if (err == 0) {
        TEST_ASSERT_NOT_NULL(bufferInfos[0].buffers[0], "Input buffer A allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[0].buffers[1], "Input buffer B allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[1].buffers[0], "Output buffer A allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[1].buffers[1], "Output buffer B allocated");
        driver->lpVtbl->DisposeBuffers(driver);
    }

    shutdown_driver(driver);
}

static int buffer_is_pattern(const unsigned char *buffer, size_t size, unsigned char pattern) {
    size_t idx = 0;
    for (idx = 0; idx < size; ++idx) {
        if (buffer[idx] != pattern) {
            return 0;
        }
    }
    return 1;
}

static void fill_pattern(unsigned char *buffer, size_t size, unsigned char pattern) {
    if (buffer && size) {
        memset(buffer, pattern, size);
    }
}

static void test_buffer_restart_clears(void) {
    TEST_BEGIN("Buffer Restart Clearing");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for restart test");
    if (!driver) {
        return;
    }

    LONG minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    ASIOError err = driver->lpVtbl->GetBufferSize(driver, &minSize, &maxSize, &preferredSize, &granularity);
    TEST_ASSERT_EQ(0, err, "GetBufferSize should succeed");

    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = 0;
    bufferInfos[0].channelNum = 0;
    bufferInfos[1].isInput = 0;
    bufferInfos[1].channelNum = 1;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;

    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };

    err = driver->lpVtbl->CreateBuffers(driver, bufferInfos, 2, preferredSize, &callbacks);
    TEST_ASSERT_EQ(0, err, "CreateBuffers should succeed");

    if (err == 0) {
        size_t buffer_bytes = (size_t)preferredSize * sizeof(float);
        fill_pattern((unsigned char *)bufferInfos[0].buffers[0], buffer_bytes, 0x7F);
        fill_pattern((unsigned char *)bufferInfos[0].buffers[1], buffer_bytes, 0x7F);
        fill_pattern((unsigned char *)bufferInfos[1].buffers[0], buffer_bytes, 0x7F);
        fill_pattern((unsigned char *)bufferInfos[1].buffers[1], buffer_bytes, 0x7F);

        driver->lpVtbl->DisposeBuffers(driver);
        err = driver->lpVtbl->CreateBuffers(driver, bufferInfos, 2, preferredSize, &callbacks);
        TEST_ASSERT_EQ(0, err, "CreateBuffers should succeed after dispose");

        if (err == 0) {
            int cleared = 1;
            cleared &= buffer_is_pattern((unsigned char *)bufferInfos[0].buffers[0], buffer_bytes, 0x00);
            cleared &= buffer_is_pattern((unsigned char *)bufferInfos[0].buffers[1], buffer_bytes, 0x00);
            cleared &= buffer_is_pattern((unsigned char *)bufferInfos[1].buffers[0], buffer_bytes, 0x00);
            cleared &= buffer_is_pattern((unsigned char *)bufferInfos[1].buffers[1], buffer_bytes, 0x00);
            TEST_ASSERT(cleared, "Buffers cleared after restart");
            driver->lpVtbl->DisposeBuffers(driver);
        }
    }

    shutdown_driver(driver);
}

static void test_buffer_validation(void) {
    TEST_BEGIN("Buffer Pointer Validation");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for pointer validation");
    if (!driver) {
        return;
    }

    LONG minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    ASIOError err = driver->lpVtbl->GetBufferSize(driver, &minSize, &maxSize, &preferredSize, &granularity);
    TEST_ASSERT_EQ(0, err, "GetBufferSize should succeed");

    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = 1;
    bufferInfos[0].channelNum = 0;
    bufferInfos[1].isInput = 0;
    bufferInfos[1].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;

    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };

    err = driver->lpVtbl->CreateBuffers(driver, bufferInfos, 2, preferredSize, &callbacks);
    TEST_ASSERT_EQ(0, err, "CreateBuffers should succeed");

    if (err == 0) {
        TEST_ASSERT_NOT_NULL(bufferInfos[0].buffers[0], "Input buffer A allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[0].buffers[1], "Input buffer B allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[1].buffers[0], "Output buffer A allocated");
        TEST_ASSERT_NOT_NULL(bufferInfos[1].buffers[1], "Output buffer B allocated");
        if (bufferInfos[0].buffers[0] && bufferInfos[0].buffers[1]) {
            float *buf0 = (float *)bufferInfos[0].buffers[0];
            float *buf1 = (float *)bufferInfos[0].buffers[1];
            buf0[0] = 1.0f;
            buf1[0] = -1.0f;
            TEST_ASSERT(buf0[0] == 1.0f && buf1[0] == -1.0f, "Buffers writable");
        }
        driver->lpVtbl->DisposeBuffers(driver);
    }

    shutdown_driver(driver);
}

int main(void) {
    printf("PipeWine ASIO Buffer Test Suite\n");
    printf("===============================\n");

    test_buffer_creation();
    test_buffer_restart_clears();
    test_buffer_validation();

    TEST_END();
}
