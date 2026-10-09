/**
 * @file test_integration.c
 * @brief Consolidated integration tests for PipeWine
 *
 * Consolidates:
 * - hybrid_test.c
 * - simple_validation_test.c
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

static void test_driver_lifecycle(void) {
    TEST_BEGIN("Driver Lifecycle");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized");
    if (!driver) {
        return;
    }

    char driverName[256] = {0};
    driver->lpVtbl->GetDriverName(driver, driverName);
    TEST_ASSERT(strlen(driverName) > 0, "Driver name is populated");

    LONG inputs = 0, outputs = 0;
    ASIOError err = driver->lpVtbl->GetChannels(driver, &inputs, &outputs);
    TEST_ASSERT_EQ(0, err, "GetChannels should succeed");

    shutdown_driver(driver);
}

static void test_buffer_lifecycle(void) {
    TEST_BEGIN("Buffer Lifecycle");

    IASIO *driver = NULL;
    TEST_ASSERT(init_driver(&driver), "Driver initialized for buffer lifecycle");
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
        err = driver->lpVtbl->Start(driver);
        TEST_ASSERT_EQ(0, err, "Start should succeed");
        driver->lpVtbl->Stop(driver);
        driver->lpVtbl->DisposeBuffers(driver);
    }

    shutdown_driver(driver);
}

int main(void) {
    printf("PipeWine Integration Test Suite\n");
    printf("===============================\n");

    test_driver_lifecycle();
    test_buffer_lifecycle();

    TEST_END();
}
