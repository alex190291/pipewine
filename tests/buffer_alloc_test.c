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

// CLSID for PipeWine
static const GUID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// Interface definitions
typedef struct IWineASIO IWineASIO;
typedef struct IWineASIOVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineASIO *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineASIO *);
    ULONG (STDMETHODCALLTYPE *Release)(IWineASIO *);
    ASIOBool (STDMETHODCALLTYPE *Init)(IWineASIO *, void *);
    void (STDMETHODCALLTYPE *GetDriverName)(IWineASIO *, char *);
    LONG (STDMETHODCALLTYPE *GetDriverVersion)(IWineASIO *);
    void (STDMETHODCALLTYPE *GetErrorMessage)(IWineASIO *, char *);
    ASIOError (STDMETHODCALLTYPE *Start)(IWineASIO *);
    ASIOError (STDMETHODCALLTYPE *Stop)(IWineASIO *);
    ASIOError (STDMETHODCALLTYPE *GetChannels)(IWineASIO *, LONG *, LONG *);
    ASIOError (STDMETHODCALLTYPE *GetLatencies)(IWineASIO *, LONG *, LONG *);
    ASIOError (STDMETHODCALLTYPE *GetBufferSize)(IWineASIO *, LONG *, LONG *, LONG *, LONG *);
    ASIOError (STDMETHODCALLTYPE *CanSampleRate)(IWineASIO *, ASIOSampleRate);
    ASIOError (STDMETHODCALLTYPE *GetSampleRate)(IWineASIO *, ASIOSampleRate *);
    ASIOError (STDMETHODCALLTYPE *SetSampleRate)(IWineASIO *, ASIOSampleRate);
    ASIOError (STDMETHODCALLTYPE *GetClockSources)(IWineASIO *, void *, LONG *);
    ASIOError (STDMETHODCALLTYPE *SetClockSource)(IWineASIO *, LONG);
    ASIOError (STDMETHODCALLTYPE *GetSamplePosition)(IWineASIO *, void *, void *);
    ASIOError (STDMETHODCALLTYPE *GetChannelInfo)(IWineASIO *, void *);
    ASIOError (STDMETHODCALLTYPE *CreateBuffers)(IWineASIO *, ASIOBufferInfo *, LONG, LONG, ASIOCallbacks *);
    ASIOError (STDMETHODCALLTYPE *DisposeBuffers)(IWineASIO *);
    ASIOError (STDMETHODCALLTYPE *ControlPanel)(IWineASIO *);
    ASIOError (STDMETHODCALLTYPE *Future)(IWineASIO *, LONG, void *);
    ASIOError (STDMETHODCALLTYPE *OutputReady)(IWineASIO *);
} IWineASIOVtbl;

struct IWineASIO {
    const IWineASIOVtbl *lpVtbl;
};

// Dummy callback functions
void bufferSwitch(LONG doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch called: index=%ld\n", doubleBufferIndex);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %.0f\n", sRate);
}

LONG asioMessage(LONG selector, LONG value, void* message, double* opt) {
    printf("ASIO message: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

void bufferSwitchTimeInfo(void *params, LONG doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info called: index=%ld\n", doubleBufferIndex);
}

int main() {
    printf("=== Simple PipeWine Buffer Allocation Test ===\n");
    
    HRESULT hr;
    IWineASIO *asio = NULL;
    
    // Initialize COM
    printf("Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }
    
    // Create ASIO driver instance
    printf("Creating PipeWine driver instance...\n");
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &IID_IUnknown, (void**)&asio);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    
    printf("Driver instance created successfully!\n");
    
    // Initialize the driver
    printf("Initializing driver...\n");
    ASIOBool initResult = asio->lpVtbl->Init(asio, NULL);
    if (!initResult) {
        printf("Failed to initialize driver\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("Driver initialized successfully!\n");
    
    // Get driver name
    char driverName[256];
    asio->lpVtbl->GetDriverName(asio, driverName);
    printf("Driver name: %s\n", driverName);
    
    // Get channel counts
    LONG inputChannels = 0, outputChannels = 0;
    ASIOError err = asio->lpVtbl->GetChannels(asio, &inputChannels, &outputChannels);
    if (err == 0) {
        printf("Channels: %ld inputs, %ld outputs\n", inputChannels, outputChannels);
    } else {
        printf("Failed to get channel count: error %ld\n", err);
    }
    
    // Set up buffer info for testing (2 inputs, 2 outputs)
    ASIOBufferInfo bufferInfo[4];
    bufferInfo[0].isInput = 1; bufferInfo[0].channelNum = 0;
    bufferInfo[1].isInput = 1; bufferInfo[1].channelNum = 1;
    bufferInfo[2].isInput = 0; bufferInfo[2].channelNum = 0;
    bufferInfo[3].isInput = 0; bufferInfo[3].channelNum = 1;
    
    // Set up callbacks
    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };
    
    // Create buffers - this should trigger the buffer allocation workflow
    printf("Creating buffers (this should trigger buffer allocation workflow)...\n");
    printf("Watch for diagnostic messages about buffer allocation...\n");
    err = asio->lpVtbl->CreateBuffers(asio, bufferInfo, 4, 1024, &callbacks);
    if (err == 0) {
        printf("SUCCESS: Buffers created successfully!\n");
        printf("Buffer pointers:\n");
        for (int i = 0; i < 4; i++) {
            printf("  Channel %d (%s): buffer[0]=%p, buffer[1]=%p\n", 
                   bufferInfo[i].channelNum,
                   bufferInfo[i].isInput ? "input" : "output",
                   bufferInfo[i].buffers[0], bufferInfo[i].buffers[1]);
        }
        
        // Clean up
        asio->lpVtbl->DisposeBuffers(asio);
    } else {
        printf("ERROR: Failed to create buffers: error %ld\n", err);
        
        // Get error message
        char errorMsg[256];
        asio->lpVtbl->GetErrorMessage(asio, errorMsg);
        printf("Error message: %s\n", errorMsg);
    }
    
    // Clean up
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    
    printf("Test completed.\n");
    return 0;
} 