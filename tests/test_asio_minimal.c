/**
 * @file test_asio_minimal.c
 * @brief Consolidated minimal ASIO tests for PipeWine
 *
 * This file consolidates the following original tests:
 * - minimal_asio_test.c
 * - minimal_com_test.c
 * - minimal_dispose_test.c
 * - minimal_test.c
 *
 * These tests verify basic COM initialization, driver creation,
 * and fundamental ASIO operations without requiring full buffer setup.
 */

#include "test_common.h"
#include <windows.h>
#include <objbase.h>

/* PipeWine CLSID */
static const CLSID CLSID_PipeWine = {
    0xA4262EE4, 0xC528, 0x4FF9,
    {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}
};

/* ASIO types */
typedef long ASIOError;
typedef long ASIOBool;
typedef double ASIOSampleRate;

#define ASE_OK 0
#define ASIOTrue 1
#define ASIOFalse 0

/* Simplified ASIO interface */
typedef struct {
    HRESULT (__stdcall *QueryInterface)(void *this, REFIID riid, void **ppvObject);
    ULONG (__stdcall *AddRef)(void *this);
    ULONG (__stdcall *Release)(void *this);
    ASIOBool (__stdcall *Init)(void *this, void *sysRef);
    void (__stdcall *GetDriverName)(void *this, char *name);
    LONG (__stdcall *GetDriverVersion)(void *this);
    void (__stdcall *GetErrorMessage)(void *this, char *string);
    ASIOError (__stdcall *Start)(void *this);
    ASIOError (__stdcall *Stop)(void *this);
    ASIOError (__stdcall *GetChannels)(void *this, LONG *numInputChannels, LONG *numOutputChannels);
    ASIOError (__stdcall *GetLatencies)(void *this, LONG *inputLatency, LONG *outputLatency);
    ASIOError (__stdcall *GetBufferSize)(void *this, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
    ASIOError (__stdcall *CanSampleRate)(void *this, ASIOSampleRate sampleRate);
    ASIOError (__stdcall *GetSampleRate)(void *this, ASIOSampleRate *sampleRate);
    ASIOError (__stdcall *SetSampleRate)(void *this, ASIOSampleRate sampleRate);
} IASIOVtbl;

typedef struct {
    IASIOVtbl *lpVtbl;
} IASIO;

/**
 * Test: COM initialization
 * Verifies that COM can be initialized correctly.
 */
void test_com_init(void) {
    TEST_BEGIN("COM Initialization");

    HRESULT hr = CoInitialize(NULL);
    TEST_ASSERT(SUCCEEDED(hr) || hr == S_FALSE, "CoInitialize should succeed");

    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
}

/**
 * Test: COM multithreaded initialization
 * Verifies that COM can be initialized in multithreaded mode.
 */
void test_com_init_multithreaded(void) {
    TEST_BEGIN("COM Multithreaded Initialization");

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    TEST_ASSERT(SUCCEEDED(hr) || hr == S_FALSE, "CoInitializeEx (MT) should succeed");

    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
}

/**
 * Test: Driver creation via COM
 * Verifies that the PipeWine driver can be created via CoCreateInstance.
 */
void test_driver_creation(void) {
    TEST_BEGIN("Driver Creation");

    HRESULT hr = CoInitialize(NULL);
    TEST_ASSERT(SUCCEEDED(hr), "COM initialization for driver test");

    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);

    TEST_ASSERT(SUCCEEDED(hr), "CoCreateInstance should succeed");
    TEST_ASSERT_NOT_NULL(pASIO, "ASIO driver pointer should be valid");

    if (pASIO) {
        pASIO->lpVtbl->Release(pASIO);
    }

    CoUninitialize();
}

/**
 * Test: Driver initialization
 * Verifies that the ASIO driver can be initialized.
 */
void test_driver_init(void) {
    TEST_BEGIN("Driver Initialization");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("SKIP: COM initialization failed\n");
        return;
    }

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);

    if (FAILED(hr) || !pASIO) {
        printf("SKIP: Driver creation failed\n");
        CoUninitialize();
        return;
    }

    ASIOBool initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    TEST_ASSERT(initResult == ASIOTrue, "ASIO Init should return ASIOTrue");

    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
}

/**
 * Test: Get driver name
 * Verifies that we can retrieve the driver name.
 */
void test_driver_name(void) {
    TEST_BEGIN("Get Driver Name");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr) || !pASIO) {
        CoUninitialize();
        return;
    }

    if (pASIO->lpVtbl->Init(pASIO, NULL) != ASIOTrue) {
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return;
    }

    char driverName[256] = {0};
    pASIO->lpVtbl->GetDriverName(pASIO, driverName);

    TEST_ASSERT(strlen(driverName) > 0, "Driver name should not be empty");
    printf("  Driver name: %s\n", driverName);

    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
}

/**
 * Test: Get channel information
 * Verifies that we can retrieve input/output channel counts.
 */
void test_channel_info(void) {
    TEST_BEGIN("Get Channel Info");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr) || !pASIO) {
        CoUninitialize();
        return;
    }

    if (pASIO->lpVtbl->Init(pASIO, NULL) != ASIOTrue) {
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return;
    }

    LONG inputs = 0, outputs = 0;
    ASIOError err = pASIO->lpVtbl->GetChannels(pASIO, &inputs, &outputs);

    TEST_ASSERT_EQ(ASE_OK, err, "GetChannels should return ASE_OK");
    TEST_ASSERT(inputs >= 0, "Input channels should be >= 0");
    TEST_ASSERT(outputs >= 0, "Output channels should be >= 0");
    printf("  Channels: %ld inputs, %ld outputs\n", inputs, outputs);

    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
}

/**
 * Test: Get sample rate
 * Verifies that we can retrieve the current sample rate.
 */
void test_sample_rate(void) {
    TEST_BEGIN("Get Sample Rate");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr) || !pASIO) {
        CoUninitialize();
        return;
    }

    if (pASIO->lpVtbl->Init(pASIO, NULL) != ASIOTrue) {
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return;
    }

    ASIOSampleRate sampleRate = 0;
    ASIOError err = pASIO->lpVtbl->GetSampleRate(pASIO, &sampleRate);

    TEST_ASSERT_EQ(ASE_OK, err, "GetSampleRate should return ASE_OK");
    TEST_ASSERT(sampleRate > 0, "Sample rate should be positive");
    printf("  Sample rate: %.0f Hz\n", sampleRate);

    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
}

/**
 * Test: Get buffer size info
 * Verifies that we can retrieve buffer size parameters.
 */
void test_buffer_size_info(void) {
    TEST_BEGIN("Get Buffer Size Info");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr) || !pASIO) {
        CoUninitialize();
        return;
    }

    if (pASIO->lpVtbl->Init(pASIO, NULL) != ASIOTrue) {
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return;
    }

    LONG minSize, maxSize, preferredSize, granularity;
    ASIOError err = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize,
                                                  &preferredSize, &granularity);

    TEST_ASSERT_EQ(ASE_OK, err, "GetBufferSize should return ASE_OK");
    TEST_ASSERT(minSize > 0, "Min buffer size should be positive");
    TEST_ASSERT(maxSize >= minSize, "Max buffer size should be >= min");
    TEST_ASSERT(preferredSize >= minSize && preferredSize <= maxSize,
                "Preferred size should be within range");

    printf("  Buffer sizes: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n",
           minSize, maxSize, preferredSize, granularity);

    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
}

/**
 * Test: Driver release
 * Verifies that the driver can be properly released.
 */
void test_driver_release(void) {
    TEST_BEGIN("Driver Release");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) return;

    IASIO *pASIO = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER,
                         &CLSID_PipeWine, (void**)&pASIO);

    if (SUCCEEDED(hr) && pASIO) {
        pASIO->lpVtbl->Init(pASIO, NULL);
        ULONG refCount = pASIO->lpVtbl->Release(pASIO);
        /* After release, refCount should be 0 (object destroyed) */
        TEST_ASSERT(refCount == 0, "Release should return 0 (object destroyed)");
    }

    CoUninitialize();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("PipeWine ASIO Minimal Test Suite\n");
    printf("================================\n");

    test_com_init();
    test_com_init_multithreaded();
    test_driver_creation();
    test_driver_init();
    test_driver_name();
    test_channel_info();
    test_sample_rate();
    test_buffer_size_info();
    test_driver_release();

    TEST_END();
}
