#include <stdio.h>
#include <windows.h>
#include <objbase.h>

// PipeWine CLSID - correct one from driver_clsid.h
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// ASIO types
typedef long ASIOError;
typedef long ASIOBool;
typedef double ASIOSampleRate;
typedef long ASIOSampleType;

#define ASE_OK 0
#define ASE_SUCCESS 0x3f4847a0
#define ASIOTrue 1
#define ASIOFalse 0

// ASIO buffer info structure
typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    long channel;
    void *buffers[2];
} ASIOBufferInfo;

// ASIO callbacks structure
typedef struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    void (*bufferSwitchTimeInfo)(void* params, long doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

// ASIO interface (more complete)
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
    ASIOError (__stdcall *GetClockSources)(void *this, void *clocks, LONG *numSources);
    ASIOError (__stdcall *SetClockSource)(void *this, LONG reference);
    ASIOError (__stdcall *GetSamplePosition)(void *this, void *sPos, void *tStamp);
    ASIOError (__stdcall *GetChannelInfo)(void *this, void *info);
    ASIOError (__stdcall *CreateBuffers)(void *this, ASIOBufferInfo *bufferInfos, LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks);
    ASIOError (__stdcall *DisposeBuffers)(void *this);
    ASIOError (__stdcall *ControlPanel)(void *this);
    ASIOError (__stdcall *Future)(void *this, LONG selector, void *opt);
    ASIOError (__stdcall *OutputReady)(void *this);
} IASIOVtbl;

typedef struct {
    IASIOVtbl *lpVtbl;
} IASIO;

// Dummy callback functions
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch called: index=%ld, direct=%d\n", doubleBufferIndex, directProcess);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("ASIO message: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

void bufferSwitchTimeInfo(void* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info called: index=%ld, direct=%d\n", doubleBufferIndex, directProcess);
}

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    
    printf("Testing PipeWine buffer creation...\n");
    fflush(stdout);
    
    // Initialize COM
    printf("Initializing COM...\n");
    fflush(stdout);
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08lx\n", hr);
        fflush(stdout);
        return 1;
    }
    printf("COM initialized\n");
    fflush(stdout);
    
    // Create the ASIO driver instance
    printf("Creating ASIO driver instance...\n");
    fflush(stdout);
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance: 0x%08lx\n", hr);
        fflush(stdout);
        CoUninitialize();
        return 1;
    }
    printf("PipeWine instance created\n");
    fflush(stdout);
    
    // Initialize the driver
    printf("Calling Init()...\n");
    fflush(stdout);
    ASIOBool initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    
    if (!initResult) {
        printf("Failed to initialize ASIO driver\n");
        fflush(stdout);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("ASIO driver initialized successfully!\n");
    fflush(stdout);
    
    // Get channel count
    LONG inputs = 0, outputs = 0;
    ASIOError channelResult = pASIO->lpVtbl->GetChannels(pASIO, &inputs, &outputs);
    if (channelResult != ASE_OK) {
        printf("Failed to get channel information: %ld\n", channelResult);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("Channels: %ld inputs, %ld outputs\n", inputs, outputs);
    
    // Get buffer size info
    LONG minSize, maxSize, preferredSize, granularity;
    ASIOError bufferSizeResult = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (bufferSizeResult != ASE_OK) {
        printf("Failed to get buffer size info: %ld\n", bufferSizeResult);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("Buffer sizes: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n", 
           minSize, maxSize, preferredSize, granularity);
    
    // Try to create buffers with minimal configuration (2 in, 2 out)
    printf("Creating buffers...\n");
    
    ASIOBufferInfo bufferInfos[4];
    // Input channels
    bufferInfos[0].isInput = ASIOTrue;
    bufferInfos[0].channel = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOTrue;
    bufferInfos[1].channel = 1;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    // Output channels
    bufferInfos[2].isInput = ASIOFalse;
    bufferInfos[2].channel = 0;
    bufferInfos[2].buffers[0] = NULL;
    bufferInfos[2].buffers[1] = NULL;
    
    bufferInfos[3].isInput = ASIOFalse;
    bufferInfos[3].channel = 1;
    bufferInfos[3].buffers[0] = NULL;
    bufferInfos[3].buffers[1] = NULL;
    
    // Set up callbacks
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    // This is where the crash might happen
    printf("About to call CreateBuffers...\n");
    ASIOError createResult = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 4, preferredSize, &callbacks);
    
    if (createResult == ASE_OK) {
        printf("Buffers created successfully!\n");
        
        printf("Buffer pointers:\n");
        for (int i = 0; i < 4; i++) {
            printf("  Channel %d (%s): buf0=%p, buf1=%p\n", 
                   bufferInfos[i].channel,
                   bufferInfos[i].isInput ? "input" : "output",
                   bufferInfos[i].buffers[0],
                   bufferInfos[i].buffers[1]);
        }
        
        printf("Sleeping for 3 seconds to observe PipeWire graph...\n");
        Sleep(3000);
        
        // Dispose buffers
        printf("Disposing buffers...\n");
        ASIOError disposeResult = pASIO->lpVtbl->DisposeBuffers(pASIO);
        if (disposeResult == ASE_OK) {
            printf("Buffers disposed successfully\n");
        } else {
            printf("Failed to dispose buffers: %ld\n", disposeResult);
        }
        
    } else {
        printf("Failed to create buffers: %ld\n", createResult);
        
        // Try to get error message
        char errorMsg[256] = {0};
        pASIO->lpVtbl->GetErrorMessage(pASIO, errorMsg);
        printf("Error message: %s\n", errorMsg);
    }
    
    // Cleanup
    printf("Releasing driver...\n");
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    printf("Test completed\n");
    return 0;
} 