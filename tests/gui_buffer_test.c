#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../rtaudio/include/asio.h"

// PipeWire ASIO driver CLSID
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    LONG minSize, maxSize, preferredSize, granularity;
    
    printf("=== GUI Buffer Size Test ===\n");
    printf("This test verifies that buffer size settings from the GUI are properly applied\n\n");
    
    // Initialize COM
    printf("1. Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("❌ Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }
    
    // Create ASIO driver instance
    printf("2. Creating PipeWire ASIO driver instance...\n");
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("❌ Failed to create PipeWine instance: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    
    // Initialize the driver
    printf("3. Initializing driver...\n");
    ASIOBool initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    if (!initResult) {
        printf("❌ Failed to initialize ASIO driver\n");
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    // Get initial buffer size info
    printf("4. Getting initial buffer size information...\n");
    ASIOError result = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (result != ASE_OK) {
        printf("❌ Failed to get buffer size info: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("   Initial buffer sizes:\n");
    printf("   - Min: %ld samples\n", minSize);
    printf("   - Max: %ld samples\n", maxSize);
    printf("   - Preferred: %ld samples\n", preferredSize);
    printf("   - Granularity: %ld\n", granularity);
    
    // Open control panel (GUI)
    printf("\n5. Opening control panel (GUI)...\n");
    printf("   Please change the buffer size in the GUI and click OK\n");
    printf("   Current preferred size: %ld samples\n", preferredSize);
    
    result = pASIO->lpVtbl->ControlPanel(pASIO);
    if (result != ASE_OK) {
        printf("❌ Failed to open control panel: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    // Wait a moment for GUI changes to be applied
    Sleep(1000);
    
    // Get buffer size info again to see if it changed
    printf("\n6. Getting buffer size information after GUI...\n");
    LONG newMinSize, newMaxSize, newPreferredSize, newGranularity;
    result = pASIO->lpVtbl->GetBufferSize(pASIO, &newMinSize, &newMaxSize, &newPreferredSize, &newGranularity);
    if (result != ASE_OK) {
        printf("❌ Failed to get buffer size info after GUI: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("   Buffer sizes after GUI:\n");
    printf("   - Min: %ld samples\n", newMinSize);
    printf("   - Max: %ld samples\n", newMaxSize);
    printf("   - Preferred: %ld samples\n", newPreferredSize);
    printf("   - Granularity: %ld\n", newGranularity);
    
    // Check if the preferred size changed
    if (newPreferredSize != preferredSize) {
        printf("\n✅ SUCCESS: Buffer size changed from %ld to %ld samples\n", preferredSize, newPreferredSize);
        printf("   The GUI buffer size setting is working correctly!\n");
    } else {
        printf("\n⚠️  WARNING: Buffer size remained the same (%ld samples)\n", preferredSize);
        printf("   Either you didn't change it in the GUI, or there's still an issue\n");
    }
    
    // Test CreateBuffers with the new preferred size
    printf("\n7. Testing CreateBuffers with preferred size...\n");
    ASIOBufferInfo bufferInfos[2];
    ASIOCallbacks callbacks = {0}; // Dummy callbacks
    
    bufferInfos[0].isInput = ASIOFalse;
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channelNum = 1;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    result = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, newPreferredSize, &callbacks);
    if (result == ASE_OK) {
        printf("✅ CreateBuffers succeeded with buffer size %ld\n", newPreferredSize);
        
        // Clean up buffers
        pASIO->lpVtbl->DisposeBuffers(pASIO);
    } else {
        printf("❌ CreateBuffers failed with buffer size %ld: %ld\n", newPreferredSize, result);
    }
    
    // Clean up
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    printf("\n=== Test Complete ===\n");
    return 0;
} 