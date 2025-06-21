#include <stdio.h>
#include <windows.h>
#include <objbase.h>
#include "asio.h"

// Simplified buffer switch callback
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("bufferSwitch called: index=%ld, directProcess=%d\n", doubleBufferIndex, directProcess);
}

// Sample rate change callback
void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("sampleRateDidChange: %.0f\n", sRate);
}

// ASIO message callback
long asioMessage(long selector, long value, void* message, double* opt) {
    printf("asioMessage: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

// Buffer size change callback
ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("bufferSwitchTimeInfo called\n");
    return NULL;
}

int main() {
    HRESULT hr;
    LPWINEASIO pASIO;
    ASIODriverInfo driverInfo = {0};
    ASIOBufferInfo bufferInfos[2];
    ASIOCallbacks callbacks;
    LONG minSize, maxSize, preferredSize, granularity;
    LONG numInputChannels, numOutputChannels;
    
    printf("=== Debug Buffer Test ===\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM\n");
        return 1;
    }
    
    // Create ASIO driver instance
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create ASIO driver instance\n");
        CoUninitialize();
        return 1;
    }
    
    printf("Driver created successfully\n");
    
    // Initialize driver
    if (!pASIO->lpVtbl->Init(pASIO, GetDesktopWindow())) {
        printf("Failed to initialize driver\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Driver initialized\n");
    
    // Get channel counts
    ASIOError result = pASIO->lpVtbl->GetChannels(pASIO, &numInputChannels, &numOutputChannels);
    if (result != ASE_OK) {
        printf("GetChannels failed: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Available channels: inputs=%ld, outputs=%ld\n", numInputChannels, numOutputChannels);
    
    // Get buffer size info
    result = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (result != ASE_OK) {
        printf("GetBufferSize failed: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("Buffer sizes: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n", 
           minSize, maxSize, preferredSize, granularity);
    
    // Set up callbacks
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    // Set up buffer configuration (1 input + 1 output)
    bufferInfos[0].isInput = ASIOTrue;
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channelNum = 0;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    printf("=== CALLING CreateBuffers ===\n");
    printf("About to call CreateBuffers with 2 channels (1 input, 1 output), buffer size %ld\n", preferredSize);
    
    result = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, preferredSize, &callbacks);
    
    printf("=== CreateBuffers returned: %ld ===\n", result);
    
    if (result == ASE_OK) {
        printf("SUCCESS: CreateBuffers completed successfully!\n");
        printf("Input buffer[0]: %p, buffer[1]: %p\n", bufferInfos[0].buffers[0], bufferInfos[0].buffers[1]);
        printf("Output buffer[0]: %p, buffer[1]: %p\n", bufferInfos[1].buffers[0], bufferInfos[1].buffers[1]);
        
        // Clean up
        pASIO->lpVtbl->DisposeBuffers(pASIO);
    } else {
        printf("FAILED: CreateBuffers returned %ld\n", result);
    }
    
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    return (result == ASE_OK) ? 0 : 1;
} 