#include <windows.h>
#include <stdio.h>
#include <objbase.h>

// ASIO definitions
typedef long ASIOBool;
typedef long ASIOError;
typedef double ASIOSampleRate;

#define ASIOTrue 1
#define ASIOFalse 0
#define ASE_OK 0

typedef struct ASIOBufferInfo
{
    ASIOBool isInput;
    LONG channelNum;
    void *buffers[2];
} ASIOBufferInfo;

typedef void (*bufferSwitch_t)(long doubleBufferIndex, ASIOBool directProcess);
typedef void (*sampleRateDidChange_t)(ASIOSampleRate sRate);
typedef long (*asioMessage_t)(long selector, long value, void* message, double* opt);
typedef void (*bufferSwitchTimeInfo_t)(void* params, long doubleBufferIndex, ASIOBool directProcess);

typedef struct ASIOCallbacks
{
    bufferSwitch_t bufferSwitch;
    sampleRateDidChange_t sampleRateDidChange;
    asioMessage_t asioMessage;
    bufferSwitchTimeInfo_t bufferSwitchTimeInfo;
} ASIOCallbacks;

typedef struct IASIO_Vtbl
{
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
    ASIOError (__stdcall *SetClockSource)(void *this, LONG index);
    ASIOError (__stdcall *GetSamplePosition)(void *this, void *sPos, void *tStamp);
    ASIOError (__stdcall *GetChannelInfo)(void *this, void *info);
    ASIOError (__stdcall *CreateBuffers)(void *this, ASIOBufferInfo *bufferInfos, LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks);
    ASIOError (__stdcall *DisposeBuffers)(void *this);
    ASIOError (__stdcall *ControlPanel)(void *this);
    ASIOError (__stdcall *Future)(void *this, LONG selector, void *opt);
    ASIOError (__stdcall *OutputReady)(void *this);
} IASIO_Vtbl;

typedef struct IASIO
{
    IASIO_Vtbl *lpVtbl;
} IASIO;

// PipeWine CLSID
static const GUID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

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
    
    printf("=== Focused Buffer Creation Test ===\n");
    fflush(stdout);
    
    // Initialize COM
    printf("Initializing COM...\n");
    fflush(stdout);
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }
    
    // Create the ASIO driver instance
    printf("Creating ASIO driver instance...\n");
    fflush(stdout);
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("Driver created successfully\n");
    fflush(stdout);
    
    // Initialize the driver
    printf("Calling Init()...\n");
    fflush(stdout);
    ASIOBool initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    if (!initResult) {
        printf("Failed to initialize ASIO driver\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("Driver initialized\n");
    fflush(stdout);
    
    // Get buffer size info
    LONG minSize, maxSize, preferredSize, granularity;
    ASIOError bufferSizeResult = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (bufferSizeResult != ASE_OK) {
        printf("Failed to get buffer size info: %ld\n", bufferSizeResult);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("Buffer size: preferred=%ld\n", preferredSize);
    fflush(stdout);
    
    // Set up minimal buffer configuration (1 input, 1 output)
    printf("Setting up buffer configuration...\n");
    fflush(stdout);
    
    ASIOBufferInfo bufferInfos[2];
    // Input channel
    bufferInfos[0].isInput = ASIOTrue;
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    // Output channel
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channelNum = 0;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    // Set up callbacks
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    // This is the critical call - where our crash likely happens
    printf("=== CALLING CreateBuffers ===\n");
    printf("About to call CreateBuffers with 2 channels, buffer size %ld\n", preferredSize);
    fflush(stdout);
    
    ASIOError createResult = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, preferredSize, &callbacks);
    
    printf("=== CreateBuffers returned: %ld ===\n", createResult);
    fflush(stdout);
    
    if (createResult == ASE_OK) {
        printf("SUCCESS: Buffers created!\n");
        printf("Input buffer: %p, %p\n", bufferInfos[0].buffers[0], bufferInfos[0].buffers[1]);
        printf("Output buffer: %p, %p\n", bufferInfos[1].buffers[0], bufferInfos[1].buffers[1]);
        
        // Dispose buffers
        printf("Disposing buffers...\n");
        pASIO->lpVtbl->DisposeBuffers(pASIO);
        printf("Buffers disposed\n");
    } else {
        printf("FAILED: CreateBuffers returned %ld\n", createResult);
        
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