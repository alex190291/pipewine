#include <stdio.h>
#include <windows.h>
#include <ole2.h>
#include <unistd.h>

// ASIO definitions
typedef double ASIOSampleRate;
typedef long ASIOBool;
typedef long ASIOError;

typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    LONG channelNum;
    void *buffers[2];
} ASIOBufferInfo;

typedef struct ASIOCallbacks {
    void (*bufferSwitch)(LONG doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    LONG (*asioMessage)(LONG selector, LONG value, void* message, double* opt);
    void (*bufferSwitchTimeInfo)(void *params, LONG doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

typedef struct IASIO_Vtbl {
    HRESULT (__stdcall *QueryInterface)(void *this, REFIID riid, void **ppvObject);
    ULONG (__stdcall *AddRef)(void *this);
    ULONG (__stdcall *Release)(void *this);
    ASIOBool (__stdcall *init)(void *this, void *sysHandle);
    void (__stdcall *getDriverName)(void *this, char *name);
    LONG (__stdcall *getDriverVersion)(void *this);
    void (__stdcall *getErrorMessage)(void *this, char *string);
    ASIOError (__stdcall *start)(void *this);
    ASIOError (__stdcall *stop)(void *this);
    ASIOError (__stdcall *getChannels)(void *this, LONG *numInputChannels, LONG *numOutputChannels);
    ASIOError (__stdcall *getLatencies)(void *this, LONG *inputLatency, LONG *outputLatency);
    ASIOError (__stdcall *getBufferSize)(void *this, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
    ASIOError (__stdcall *canSampleRate)(void *this, ASIOSampleRate sampleRate);
    ASIOError (__stdcall *getSampleRate)(void *this, ASIOSampleRate *sampleRate);
    ASIOError (__stdcall *setSampleRate)(void *this, ASIOSampleRate sampleRate);
    ASIOError (__stdcall *getClockSources)(void *this, void *clocks, LONG *numSources);
    ASIOError (__stdcall *setClockSource)(void *this, LONG reference);
    ASIOError (__stdcall *getSamplePosition)(void *this, void *sPos, void *tStamp);
    ASIOError (__stdcall *getChannelInfo)(void *this, void *info);
    ASIOError (__stdcall *createBuffers)(void *this, ASIOBufferInfo *bufferInfos, LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks);
    ASIOError (__stdcall *disposeBuffers)(void *this);
    ASIOError (__stdcall *controlPanel)(void *this);
    ASIOError (__stdcall *future)(void *this, LONG selector, void *opt);
    ASIOError (__stdcall *outputReady)(void *this);
} IASIO_Vtbl;

typedef struct IASIO {
    IASIO_Vtbl *lpVtbl;
} IASIO;

// PipeWire ASIO CLSID
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};
static const IID IID_IASIO = {0x8B85C19A, 0x1B7A, 0x11D5, {0x9F, 0x85, 0x00, 0x60, 0x08, 0x3B, 0xF4, 0x3D}};

// ASIO callback implementations
void bufferSwitch(LONG doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer callback: index=%ld\n", doubleBufferIndex);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed to: %.0f Hz\n", sRate);
}

LONG asioMessage(LONG selector, LONG value, void* message, double* opt) {
    printf("ASIO message: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

void bufferSwitchTimeInfo(void *params, LONG doubleBufferIndex, ASIOBool directProcess) {
    bufferSwitch(doubleBufferIndex, directProcess);
}

const char* asio_error_to_string(ASIOError error) {
    switch (error) {
        case 0: return "ASE_OK";
        case 1: return "ASE_SUCCESS";
        case -1000: return "ASE_NotPresent";
        case -999: return "ASE_HWMalfunction";
        case -998: return "ASE_InvalidParameter";
        case -997: return "ASE_InvalidMode";
        case -996: return "ASE_SPNotAdvancing";
        case -995: return "ASE_NoClock";
        case -994: return "ASE_NoMemory";
        default: return "Unknown error";
    }
}

int main() {
    printf("=== Debug Threading Test ===\n\n");
    
    // Initialize COM
    printf("1. Initializing COM...\n");
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("❌ CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("✓ COM initialized\n");
    
    // Create ASIO driver instance
    printf("\n2. Creating PipeWire ASIO driver instance...\n");
    IASIO *asio = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, &IID_IASIO, (void**)&asio);
    if (FAILED(hr)) {
        printf("❌ CoCreateInstance failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver instance created\n");
    
    // Initialize driver
    printf("\n3. Initializing driver...\n");
    ASIOBool init_result = asio->lpVtbl->init(asio, NULL);
    if (!init_result) {
        printf("❌ Driver initialization failed\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver initialized successfully\n");
    
    // Get channel count
    printf("\n4. Getting channels...\n");
    LONG input_channels, output_channels;
    ASIOError error = asio->lpVtbl->getChannels(asio, &input_channels, &output_channels);
    if (error != 0) {
        printf("❌ getChannels failed: %s\n", asio_error_to_string(error));
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("✓ Channels: %ld inputs, %ld outputs\n", input_channels, output_channels);
    
    // Test buffer creation with minimal setup (2 channels only)
    printf("\n5. Testing buffer creation with 2 channels (debug mode)...\n");
    
    ASIOBufferInfo buffer_infos[2];
    buffer_infos[0].isInput = 1;
    buffer_infos[0].channelNum = 0;
    buffer_infos[0].buffers[0] = NULL;
    buffer_infos[0].buffers[1] = NULL;
    
    buffer_infos[1].isInput = 0;
    buffer_infos[1].channelNum = 0;
    buffer_infos[1].buffers[0] = NULL;
    buffer_infos[1].buffers[1] = NULL;
    
    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };
    
    printf("   Creating buffers with 2 channels, buffer size 512...\n");
    printf("   This should test the threading fix...\n");
    
    error = asio->lpVtbl->createBuffers(asio, buffer_infos, 2, 512, &callbacks);
    if (error != 0) {
        printf("❌ createBuffers failed: %s\n", asio_error_to_string(error));
        
        // Try to get error message
        char errorMsg[256] = {0};
        asio->lpVtbl->getErrorMessage(asio, errorMsg);
        printf("   Error message: %s\n", errorMsg);
        
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("✓ Buffers created successfully!\n");
    
    // Verify buffer pointers are valid
    for (int i = 0; i < 2; i++) {
        if (!buffer_infos[i].buffers[0] || !buffer_infos[i].buffers[1]) {
            printf("❌ Buffer %d has NULL pointers\n", i);
        } else {
            printf("✓ Buffer %d: [0]=%p, [1]=%p\n", i, buffer_infos[i].buffers[0], buffer_infos[i].buffers[1]);
        }
    }
    
    // Cleanup
    printf("\n6. Cleaning up...\n");
    asio->lpVtbl->disposeBuffers(asio);
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    
    printf("\n=== DEBUG THREADING TEST COMPLETED! ===\n");
    
    return 0;
} 