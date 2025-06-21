#include <stdio.h>
#include <windows.h>
#include <ole2.h>

// CLSID for PipeWine driver
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// ASIO interface IID (use same as CLSID for simplicity)
static const IID IID_ASIO = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// Simplified ASIO interface
typedef struct {
    void *lpVtbl;
} IASIO;

typedef struct {
    // IUnknown methods
    HRESULT (*QueryInterface)(IASIO *this, REFIID riid, void **ppvObject);
    ULONG (*AddRef)(IASIO *this);
    ULONG (*Release)(IASIO *this);
    
    // ASIO methods
    int (*Init)(IASIO *this, void *sysRef);
    void (*GetDriverName)(IASIO *this, char *name);
    int (*GetDriverVersion)(IASIO *this);
    void (*GetErrorMessage)(IASIO *this, char *string);
    int (*Start)(IASIO *this);
    int (*Stop)(IASIO *this);
    int (*GetChannels)(IASIO *this, long *numInputChannels, long *numOutputChannels);
    int (*GetSampleRate)(IASIO *this, double *sampleRate);
} IASIOVtbl;

int main() {
    printf("Minimal ASIO Test Starting...\n");
    fflush(stdout);
    
    // Initialize COM
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }
    
    // Create ASIO driver instance
    IASIO* asio = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, &IID_ASIO, (void**)&asio);
    if (FAILED(hr)) {
        printf("Failed to create ASIO instance: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    
    printf("ASIO driver instance created successfully\n");
    fflush(stdout);
    
    IASIOVtbl *vtbl = (IASIOVtbl*)asio->lpVtbl;
    
    // Initialize the driver
    int result = vtbl->Init(asio, NULL);
    if (result == 0) {  // ASIOFalse means failure, ASIOTrue (1) means success
        printf("ASIO init failed: %d\n", result);
        vtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("ASIO driver initialized successfully\n");
    fflush(stdout);
    
    // Get driver name
    char driverName[256];
    vtbl->GetDriverName(asio, driverName);
    printf("Driver name: %s\n", driverName);
    fflush(stdout);
    
    // Get channel counts
    long inputChannels, outputChannels;
    int err = vtbl->GetChannels(asio, &inputChannels, &outputChannels);
    if (err == 0) {
        printf("Channels: %ld inputs, %ld outputs\n", inputChannels, outputChannels);
    } else {
        printf("Failed to get channel counts: %d\n", err);
    }
    fflush(stdout);
    
    // Get sample rate
    double sampleRate;
    err = vtbl->GetSampleRate(asio, &sampleRate);
    if (err == 0) {
        printf("Sample rate: %.0f Hz\n", sampleRate);
    } else {
        printf("Failed to get sample rate: %d\n", err);
    }
    fflush(stdout);
    
    // Cleanup
    vtbl->Release(asio);
    CoUninitialize();
    
    printf("Test completed successfully\n");
    fflush(stdout);
    
    return 0;
} 