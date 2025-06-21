#include <stdio.h>
#include <windows.h>
#include <ole2.h>

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

// Dummy callbacks
void bufferSwitch(LONG doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch called: index=%ld, direct=%ld\n", doubleBufferIndex, directProcess);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %f\n", sRate);
}

LONG asioMessage(LONG selector, LONG value, void* message, double* opt) {
    printf("ASIO message: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

void bufferSwitchTimeInfo(void *params, LONG doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info called\n");
}

int main() {
    printf("=== Buffer Focus Test ===\n");
    
    // Initialize COM
    printf("1. Initializing COM...\n");
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("❌ CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("✓ COM initialized\n");
    
    // Create ASIO driver instance
    printf("2. Creating PipeWire ASIO driver instance...\n");
    IASIO *asio = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, &IID_IASIO, (void**)&asio);
    if (FAILED(hr)) {
        printf("❌ CoCreateInstance failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver instance created\n");
    
    // Initialize driver
    printf("3. Initializing driver...\n");
    ASIOBool init_result = asio->lpVtbl->init(asio, GetDesktopWindow());
    if (!init_result) {
        printf("❌ Driver init failed\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver initialized\n");
    
    // Get driver info
    char driverName[256];
    asio->lpVtbl->getDriverName(asio, driverName);
    printf("4. Driver name: %s\n", driverName);
    
    LONG version = asio->lpVtbl->getDriverVersion(asio);
    printf("5. Driver version: %ld\n", version);
    
    // Get channel counts
    LONG numInputs = 0, numOutputs = 0;
    ASIOError err = asio->lpVtbl->getChannels(asio, &numInputs, &numOutputs);
    if (err != 0) {
        printf("❌ getChannels failed: %ld\n", err);
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("6. Channels: %ld inputs, %ld outputs\n", numInputs, numOutputs);
    
    // Get buffer size info
    LONG minSize, maxSize, preferredSize, granularity;
    err = asio->lpVtbl->getBufferSize(asio, &minSize, &maxSize, &preferredSize, &granularity);
    if (err != 0) {
        printf("❌ getBufferSize failed: %ld\n", err);
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("7. Buffer sizes: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n", 
           minSize, maxSize, preferredSize, granularity);
    
    // Set up buffer info for 2 inputs + 2 outputs
    ASIOBufferInfo bufferInfos[4];
    memset(bufferInfos, 0, sizeof(bufferInfos));
    
    // Input channels
    bufferInfos[0].isInput = 1;
    bufferInfos[0].channelNum = 0;
    bufferInfos[1].isInput = 1;
    bufferInfos[1].channelNum = 1;
    
    // Output channels  
    bufferInfos[2].isInput = 0;
    bufferInfos[2].channelNum = 0;
    bufferInfos[3].isInput = 0;
    bufferInfos[3].channelNum = 1;
    
    // Set up callbacks
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    printf("8. Creating buffers (buffer size: %ld)...\n", preferredSize);
    printf("   This is where the test usually hangs - let's see what happens...\n");
    
    // Create buffers - THIS IS THE CRITICAL TEST
    err = asio->lpVtbl->createBuffers(asio, bufferInfos, 4, preferredSize, &callbacks);
    if (err != 0) {
        printf("❌ createBuffers failed: %ld\n", err);
        char errorMsg[256];
        asio->lpVtbl->getErrorMessage(asio, errorMsg);
        printf("   Error message: %s\n", errorMsg);
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("✓ Buffers created successfully!\n");
    
    // Check buffer pointers
    printf("9. Checking buffer pointers:\n");
    for (int i = 0; i < 4; i++) {
        printf("   Channel %d (%s): buffer[0]=%p, buffer[1]=%p\n", 
               i, bufferInfos[i].isInput ? "input" : "output",
               bufferInfos[i].buffers[0], bufferInfos[i].buffers[1]);
        
        if (bufferInfos[i].buffers[0] == NULL || bufferInfos[i].buffers[1] == NULL) {
            printf("   ❌ NULL buffer pointer detected!\n");
        } else {
            printf("   ✓ Buffer pointers are valid\n");
        }
    }
    
    // Clean up
    printf("10. Cleaning up...\n");
    asio->lpVtbl->disposeBuffers(asio);
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    
    printf("=== Test completed successfully ===\n");
    return 0;
} 