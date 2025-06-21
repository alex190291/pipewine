#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>

#define IEEE754_64FLOAT 1
#undef NATIVE_INT64
#include <asio.h>

// Test callbacks - minimal implementation
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("✓ bufferSwitch called: index=%ld, directProcess=%d\n", doubleBufferIndex, directProcess);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("✓ sampleRateDidChange called: rate=%f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("✓ asioMessage called: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("✓ bufferSwitchTimeInfo called: index=%ld, directProcess=%d\n", doubleBufferIndex, directProcess);
    return params;
}

int main() {
    printf("=== ASIO Buffer Validation Test ===\n");
    
    // Load the ASIO driver
    HMODULE hModule = LoadLibrary("pipewine64.dll.so");
    if (!hModule) {
        printf("❌ Failed to load pipewine64.dll.so\n");
        return 1;
    }
    printf("✓ Loaded ASIO driver\n");
    
    // Get the driver entry point
    typedef ASIOError (*ASIOInit_t)(void* sysRef);
    typedef ASIOError (*ASIOGetChannels_t)(long* numInputChannels, long* numOutputChannels);
    typedef ASIOError (*ASIOGetBufferSize_t)(long* minSize, long* maxSize, long* preferredSize, long* granularity);
    typedef ASIOError (*ASIOCreateBuffers_t)(ASIOBufferInfo* bufferInfo, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
    typedef ASIOError (*ASIODisposeBuffers_t)(void);
    typedef ASIOError (*ASIOStart_t)(void);
    typedef ASIOError (*ASIOStop_t)(void);
    typedef void (*ASIOExit_t)(void);
    
    ASIOInit_t ASIOInit = (ASIOInit_t)GetProcAddress(hModule, "ASIOInit");
    ASIOGetChannels_t ASIOGetChannels = (ASIOGetChannels_t)GetProcAddress(hModule, "ASIOGetChannels");
    ASIOGetBufferSize_t ASIOGetBufferSize = (ASIOGetBufferSize_t)GetProcAddress(hModule, "ASIOGetBufferSize");
    ASIOCreateBuffers_t ASIOCreateBuffers = (ASIOCreateBuffers_t)GetProcAddress(hModule, "ASIOCreateBuffers");
    ASIODisposeBuffers_t ASIODisposeBuffers = (ASIODisposeBuffers_t)GetProcAddress(hModule, "ASIODisposeBuffers");
    ASIOStart_t ASIOStart = (ASIOStart_t)GetProcAddress(hModule, "ASIOStart");
    ASIOStop_t ASIOStop = (ASIOStop_t)GetProcAddress(hModule, "ASIOStop");
    ASIOExit_t ASIOExit = (ASIOExit_t)GetProcAddress(hModule, "ASIOExit");
    
    if (!ASIOInit || !ASIOGetChannels || !ASIOGetBufferSize || !ASIOCreateBuffers) {
        printf("❌ Failed to get ASIO function pointers\n");
        FreeLibrary(hModule);
        return 1;
    }
    printf("✓ Got ASIO function pointers\n");
    
    // Initialize ASIO
    ASIOError result = ASIOInit(NULL);
    if (result != ASE_OK) {
        printf("❌ ASIOInit failed: %d\n", result);
        FreeLibrary(hModule);
        return 1;
    }
    printf("✓ ASIO initialized\n");
    
    // Get channel info
    long numInputs = 0, numOutputs = 0;
    result = ASIOGetChannels(&numInputs, &numOutputs);
    if (result != ASE_OK) {
        printf("❌ ASIOGetChannels failed: %d\n", result);
        ASIOExit();
        FreeLibrary(hModule);
        return 1;
    }
    printf("✓ Channels: %ld inputs, %ld outputs\n", numInputs, numOutputs);
    
    // Get buffer size info
    long minSize, maxSize, preferredSize, granularity;
    result = ASIOGetBufferSize(&minSize, &maxSize, &preferredSize, &granularity);
    if (result != ASE_OK) {
        printf("❌ ASIOGetBufferSize failed: %d\n", result);
        ASIOExit();
        FreeLibrary(hModule);
        return 1;
    }
    printf("✓ Buffer sizes: min=%ld, max=%ld, preferred=%ld, granularity=%ld\n", 
           minSize, maxSize, preferredSize, granularity);
    
    // Setup buffer info for minimal test (1 input + 1 output)
    long numChannels = 2;
    long bufferSize = preferredSize;
    ASIOBufferInfo bufferInfo[2];
    
    // Input channel
    bufferInfo[0].isInput = ASIOTrue;
    bufferInfo[0].channelNum = 0;
    bufferInfo[0].buffers[0] = NULL;
    bufferInfo[0].buffers[1] = NULL;
    
    // Output channel  
    bufferInfo[1].isInput = ASIOFalse;
    bufferInfo[1].channelNum = 0;
    bufferInfo[1].buffers[0] = NULL;
    bufferInfo[1].buffers[1] = NULL;
    
    // Setup callbacks
    ASIOCallbacks callbacks;
    callbacks.bufferSwitch = bufferSwitch;
    callbacks.sampleRateDidChange = sampleRateDidChange;
    callbacks.asioMessage = asioMessage;
    callbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    
    printf("Creating buffers with %ld channels, buffer size %ld...\n", numChannels, bufferSize);
    
    // Create buffers - this is where the bug should manifest
    result = ASIOCreateBuffers(bufferInfo, numChannels, bufferSize, &callbacks);
    if (result != ASE_OK) {
        printf("❌ ASIOCreateBuffers failed: %d\n", result);
        ASIOExit();
        FreeLibrary(hModule);
        return 1;
    }
    printf("✓ ASIOCreateBuffers succeeded\n");
    
    // Validate buffer pointers
    printf("\n=== Buffer Pointer Validation ===\n");
    int failed_buffers = 0;
    
    for (int i = 0; i < numChannels; i++) {
        printf("Channel %d (%s):\n", i, bufferInfo[i].isInput ? "input" : "output");
        
        if (bufferInfo[i].buffers[0] == NULL) {
            printf("  ❌ Buffer 0 is NULL\n");
            failed_buffers++;
        } else {
            printf("  ✓ Buffer 0: %p\n", bufferInfo[i].buffers[0]);
        }
        
        if (bufferInfo[i].buffers[1] == NULL) {
            printf("  ❌ Buffer 1 is NULL\n");
            failed_buffers++;
        } else {
            printf("  ✓ Buffer 1: %p\n", bufferInfo[i].buffers[1]);
        }
    }
    
    if (failed_buffers > 0) {
        printf("\n❌ CRITICAL: %d buffer pointers are NULL!\n", failed_buffers);
    } else {
        printf("\n✓ All buffer pointers are valid\n");
        
        // Test buffer write/read
        printf("\n=== Buffer Write/Read Test ===\n");
        for (int i = 0; i < numChannels; i++) {
            float *buf0 = (float*)bufferInfo[i].buffers[0];
            float *buf1 = (float*)bufferInfo[i].buffers[1];
            
            // Write test pattern
            buf0[0] = 1.0f;
            buf0[1] = -1.0f;
            buf1[0] = 0.5f;
            buf1[1] = -0.5f;
            
            // Read back and verify
            if (buf0[0] == 1.0f && buf0[1] == -1.0f && buf1[0] == 0.5f && buf1[1] == -0.5f) {
                printf("  ✓ Channel %d buffer read/write test passed\n", i);
            } else {
                printf("  ❌ Channel %d buffer read/write test failed\n", i);
            }
        }
    }
    
    // Cleanup
    if (ASIODisposeBuffers) {
        ASIODisposeBuffers();
        printf("✓ Buffers disposed\n");
    }
    
    if (ASIOExit) {
        ASIOExit();
        printf("✓ ASIO exited\n");
    }
    
    FreeLibrary(hModule);
    printf("✓ Driver unloaded\n");
    
    printf("\n=== Test Complete ===\n");
    if (failed_buffers > 0) {
        printf("❌ RESULT: Buffer allocation test FAILED\n");
        return 1;
    } else {
        printf("✓ RESULT: Buffer allocation test PASSED\n");
        return 0;
    }
} 