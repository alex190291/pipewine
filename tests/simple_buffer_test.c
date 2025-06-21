#include <stdio.h>
#include <windows.h>
#include <objbase.h>

// PipeWine CLSID
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// ASIO types
typedef long ASIOError;
typedef long ASIOBool;
typedef double ASIOSampleRate;

#define ASE_OK 0
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

// Minimal ASIO interface
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

// Dummy callbacks
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch: %ld\n", doubleBufferIndex);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("ASIO message: %ld\n", selector);
    return 0;
}

void bufferSwitchTimeInfo(void* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info: %ld\n", doubleBufferIndex);
}

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    
    printf("Simple PipeWine buffer test\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("COM init failed: 0x%08lx\n", hr);
        return 1;
    }
    
    // Create driver
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Driver creation failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    
    printf("Driver created successfully\n");
    
    // Initialize
    if (!pASIO->lpVtbl->Init(pASIO, NULL)) {
        printf("Driver init failed\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Driver initialized\n");
    
    // Get buffer size
    LONG minSize, maxSize, preferredSize, granularity;
    if (pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity) != ASE_OK) {
        printf("GetBufferSize failed\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Buffer size: preferred=%ld\n", preferredSize);
    
    // Create minimal buffers (1 in, 1 out)
    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = ASIOTrue;
    bufferInfos[0].channel = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channel = 0;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    printf("About to create buffers...\n");
    ASIOError result = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, preferredSize, &callbacks);
    
    if (result == ASE_OK) {
        printf("SUCCESS: Buffers created!\n");
        printf("Input buffer: %p, %p\n", bufferInfos[0].buffers[0], bufferInfos[0].buffers[1]);
        printf("Output buffer: %p, %p\n", bufferInfos[1].buffers[0], bufferInfos[1].buffers[1]);
        
        // Dispose buffers
        pASIO->lpVtbl->DisposeBuffers(pASIO);
        printf("Buffers disposed\n");
    } else {
        printf("FAILED: CreateBuffers returned %ld\n", result);
    }
    
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    printf("Test completed\n");
    return 0;
} 