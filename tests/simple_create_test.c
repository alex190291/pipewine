#include <stdio.h>
#include <windows.h>
#include <objbase.h>
#include "asio.h"

// ASIO callback functions (minimal implementation)
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch: index=%ld, direct=%d\n", doubleBufferIndex, directProcess);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("ASIO message: selector=0x%lx, value=%ld\n", selector, value);
    return 0;
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info: index=%ld, direct=%d\n", doubleBufferIndex, directProcess);
    return NULL;
}

int main() {
    HRESULT hr;
    IWineASIO *asio = NULL;
    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };
    
    printf("=== Simple CreateBuffers Test ===\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("FAILED: CoInitialize failed with 0x%08lx\n", hr);
        return 1;
    }
    
    // Create ASIO instance
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER, &IID_IWineASIO, (void**)&asio);
    if (FAILED(hr)) {
        printf("FAILED: CoCreateInstance failed with 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("SUCCESS: Created PipeWine instance\n");
    
    // Initialize ASIO
    if (!asio->lpVtbl->Init(asio, NULL)) {
        printf("FAILED: ASIO Init failed\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    printf("SUCCESS: ASIO driver initialized\n");
    
    // Test CreateBuffers with minimal setup
    ASIOBufferInfo bufferInfo[4];
    
    // Setup 2 input and 2 output channels
    bufferInfo[0].isInput = ASIOTrue;
    bufferInfo[0].channelNum = 0;
    bufferInfo[0].buffers[0] = NULL;
    bufferInfo[0].buffers[1] = NULL;
    
    bufferInfo[1].isInput = ASIOTrue;
    bufferInfo[1].channelNum = 1;
    bufferInfo[1].buffers[0] = NULL;
    bufferInfo[1].buffers[1] = NULL;
    
    bufferInfo[2].isInput = ASIOFalse;
    bufferInfo[2].channelNum = 0;
    bufferInfo[2].buffers[0] = NULL;
    bufferInfo[2].buffers[1] = NULL;
    
    bufferInfo[3].isInput = ASIOFalse;
    bufferInfo[3].channelNum = 1;
    bufferInfo[3].buffers[0] = NULL;
    bufferInfo[3].buffers[1] = NULL;
    
    printf("Creating buffers with 4 channels, buffer size 1024...\n");
    ASIOError result = asio->lpVtbl->CreateBuffers(asio, bufferInfo, 4, 1024, &callbacks);
    
    if (result == ASE_OK) {
        printf("SUCCESS: CreateBuffers completed successfully\n");
        
        // Clean up
        asio->lpVtbl->DisposeBuffers(asio);
        printf("SUCCESS: DisposeBuffers completed\n");
    } else {
        printf("FAILED: CreateBuffers failed with error %d\n", result);
    }
    
    // Clean up
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    
    printf("Test completed.\n");
    return (result == ASE_OK) ? 0 : 1;
} 