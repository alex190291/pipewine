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

static const CLSID CLSID_PipeWine = {
    0xA4262EE4, 0xC528, 0x4FF9,
    {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}
};

/* ASIO types */
typedef long ASIOError;
typedef long ASIOBool;

typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    long channel;
    void *buffers[2];
} ASIOBufferInfo;

typedef struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(double sRate);
    long (*asioMessage)(long selector, long value, void *message, double *opt);
    void (*bufferSwitchTimeInfo)(void *params, long doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

typedef struct {
    HRESULT (__stdcall *QueryInterface)(void *self, REFIID riid, void **ppvObject);
    ULONG (__stdcall *AddRef)(void *self);
    ULONG (__stdcall *Release)(void *self);
    ASIOBool (__stdcall *Init)(void *self, void *sysRef);
    void (__stdcall *GetDriverName)(void *self, char *name);
    long (__stdcall *GetDriverVersion)(void *self);
    void (__stdcall *GetErrorMessage)(void *self, char *string);
    ASIOError (__stdcall *Start)(void *self);
    ASIOError (__stdcall *Stop)(void *self);
    ASIOError (__stdcall *GetChannels)(void *self, long *numInputChannels, long *numOutputChannels);
    ASIOError (__stdcall *GetLatencies)(void *self, long *inputLatency, long *outputLatency);
    ASIOError (__stdcall *GetBufferSize)(void *self, long *minSize, long *maxSize, long *preferredSize, long *granularity);
    ASIOError (__stdcall *CanSampleRate)(void *self, double sampleRate);
    ASIOError (__stdcall *GetSampleRate)(void *self, double *sampleRate);
    ASIOError (__stdcall *SetSampleRate)(void *self, double sampleRate);
    ASIOError (__stdcall *GetClockSources)(void *self, void *clocks, long *numSources);
    ASIOError (__stdcall *SetClockSource)(void *self, long reference);
    ASIOError (__stdcall *GetSamplePosition)(void *self, void *sPos, void *tStamp);
    ASIOError (__stdcall *GetChannelInfo)(void *self, void *info);
    ASIOError (__stdcall *CreateBuffers)(void *self, ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks);
    ASIOError (__stdcall *DisposeBuffers)(void *self);
    ASIOError (__stdcall *ControlPanel)(void *self);
    ASIOError (__stdcall *Future)(void *self, long selector, void *opt);
    ASIOError (__stdcall *OutputReady)(void *self);
} IASIOVtbl;

typedef struct {
    IASIOVtbl *lpVtbl;
} IASIO;

static void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    (void)doubleBufferIndex;
    (void)directProcess;
}

static void sampleRateDidChange(double sRate) {
    (void)sRate;
}

static long asioMessage(long selector, long value, void *message, double *opt) {
    (void)selector;
    (void)value;
    (void)message;
    (void)opt;
    return 0;
}

static void bufferSwitchTimeInfo(void *params, long doubleBufferIndex, ASIOBool directProcess) {
    (void)params;
    (void)doubleBufferIndex;
    (void)directProcess;
}

static int create_driver(IASIO **out_driver) {
    HRESULT hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IUnknown, (void **)out_driver);
    if (FAILED(hr)) {
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

    long inputs = 0, outputs = 0;
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

    long minSize = 0, maxSize = 0, preferredSize = 0, granularity = 0;
    ASIOError err = driver->lpVtbl->GetBufferSize(driver, &minSize, &maxSize, &preferredSize, &granularity);
    TEST_ASSERT_EQ(0, err, "GetBufferSize should succeed");

    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = 1;
    bufferInfos[0].channel = 0;
    bufferInfos[1].isInput = 0;
    bufferInfos[1].channel = 0;
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
