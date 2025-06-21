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
    printf("Buffer switch called\n");
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed\n");
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("ASIO message\n");
    return 0;
}

void bufferSwitchTimeInfo(void* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info called\n");
}

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    
    printf("=== Minimal DisposeBuffers Test ===\n");
    fflush(stdout);
    
    // Initialize COM
    printf("Step 1: Initializing COM...\n");
    fflush(stdout);
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM\n");
        return 1;
    }
    
    // Create the ASIO driver instance
    printf("Step 2: Creating ASIO driver instance...\n");
    fflush(stdout);
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance\n");
        CoUninitialize();
        return 1;
    }
    
    // Initialize the driver
    printf("Step 3: Calling Init()...\n");
    fflush(stdout);
    ASIOBool initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    if (!initResult) {
        printf("Failed to initialize ASIO driver\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    // Get buffer size info
    printf("Step 4: Getting buffer size...\n");
    fflush(stdout);
    LONG minSize, maxSize, preferredSize, granularity;
    ASIOError bufferSizeResult = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (bufferSizeResult != ASE_OK) {
        printf("Failed to get buffer size info\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    // Set up minimal buffer configuration
    printf("Step 5: Setting up buffers...\n");
    fflush(stdout);
    
    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = ASIOTrue;
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channelNum = 0;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    printf("Step 6: Calling CreateBuffers...\n");
    fflush(stdout);
    ASIOError createResult = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, preferredSize, &callbacks);
    
    if (createResult != ASE_OK) {
        printf("CreateBuffers failed: %ld\n", createResult);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Step 7: Buffers created successfully!\n");
    fflush(stdout);
    
    // This is the critical test - DisposeBuffers
    printf("Step 8: About to call DisposeBuffers...\n");
    fflush(stdout);
    
    printf("Step 8a: Calling DisposeBuffers NOW...\n");
    fflush(stdout);
    
    ASIOError disposeResult = pASIO->lpVtbl->DisposeBuffers(pASIO);
    
    printf("Step 9: DisposeBuffers returned: %ld\n", disposeResult);
    fflush(stdout);
    
    // Cleanup
    printf("Step 10: Releasing driver...\n");
    fflush(stdout);
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    printf("Test completed successfully!\n");
    return 0;
} 