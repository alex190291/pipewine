#include <stdio.h>
#include <windows.h>
#include <mmsystem.h>
#include "asio.h"

static ASIODriverInfo driverInfo;
static ASIOCallbacks asioCallbacks;
static ASIOBufferInfo bufferInfos[4];
static ASIOChannelInfo channelInfos[4];
static int buffer_switch_count = 0;
static LONG expected_buffer_size = 0;
static int timing_errors = 0;
static DWORD last_callback_time = 0;

void bufferSwitch(long index, ASIOBool processNow) {
    DWORD current_time = timeGetTime();
    buffer_switch_count++;
    
    if (last_callback_time != 0) {
        DWORD time_diff = current_time - last_callback_time;
        DWORD expected_time = (expected_buffer_size * 1000) / 48000; // Assuming 48kHz
        
        // Allow 10% tolerance
        if (time_diff < expected_time * 0.9 || time_diff > expected_time * 1.1) {
            timing_errors++;
            if (timing_errors <= 5) {
                printf("Timing error #%d: Expected ~%dms, got %dms\n", 
                       timing_errors, expected_time, time_diff);
            }
        }
    }
    
    last_callback_time = current_time;
    
    if (buffer_switch_count % 100 == 0) {
        printf("Buffer switch #%d - timing errors so far: %d\n", buffer_switch_count, timing_errors);
    }
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow) {
    bufferSwitch(index, processNow);
    return timeInfo;
}

void sampleRateChanged(ASIOSampleRate sRate) {
    printf("Sample rate changed to: %f\n", sRate);
}

long asioMessages(long selector, long value, void* message, double* opt) {
    return 0;
}

int main() {
    HRESULT hr;
    ASIOError result;
    LONG minSize, maxSize, preferredSize, granularity;
    ASIOSampleRate sampleRate;
    
    printf("Testing PipeWire ASIO Fixed Buffer Length Implementation\n");
    printf("=======================================================\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM\n");
        return 1;
    }
    
    // Create ASIO driver instance
    CLSID clsid = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};
    IUnknown* pUnknown = NULL;
    hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void**)&pUnknown);
    if (FAILED(hr)) {
        printf("Failed to create ASIO driver instance\n");
        CoUninitialize();
        return 1;
    }
    
    // Query for ASIO interface
    IASIO* asio = NULL;
    hr = pUnknown->lpVtbl->QueryInterface(pUnknown, &IID_IASIO, (void**)&asio);
    if (FAILED(hr)) {
        printf("Failed to query ASIO interface\n");
        pUnknown->lpVtbl->Release(pUnknown);
        CoUninitialize();
        return 1;
    }
    
    // Initialize driver
    if (!asio->lpVtbl->init(asio, NULL)) {
        printf("Failed to initialize ASIO driver\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("✓ ASIO driver initialized successfully\n");
    
    // Get buffer size info
    result = asio->lpVtbl->getBufferSize(asio, &minSize, &maxSize, &preferredSize, &granularity);
    if (result != ASE_OK) {
        printf("Failed to get buffer size info\n");
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("Buffer sizes - Min: %d, Max: %d, Preferred: %d, Granularity: %d\n", 
           minSize, maxSize, preferredSize, granularity);
    
    expected_buffer_size = preferredSize;
    
    // Check if buffer size is fixed
    if (minSize == maxSize && granularity == 0) {
        printf("✓ Fixed buffer size detected: %d samples\n", minSize);
    } else {
        printf("⚠ Variable buffer size detected - this may cause timing issues\n");
    }
    
    // Get sample rate
    result = asio->lpVtbl->getSampleRate(asio, &sampleRate);
    if (result == ASE_OK) {
        printf("Sample rate: %f Hz\n", sampleRate);
    }
    
    // Set up callbacks
    asioCallbacks.bufferSwitch = bufferSwitch;
    asioCallbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfo;
    asioCallbacks.sampleRateDidChange = sampleRateChanged;
    asioCallbacks.asioMessage = asioMessages;
    
    // Set up buffer info for stereo output
    bufferInfos[0].isInput = ASIOFalse;
    bufferInfos[0].channelNum = 0;
    bufferInfos[1].isInput = ASIOFalse;
    bufferInfos[1].channelNum = 1;
    
    // Create buffers
    result = asio->lpVtbl->createBuffers(asio, bufferInfos, 2, preferredSize, &asioCallbacks);
    if (result != ASE_OK) {
        printf("Failed to create buffers: %d\n", result);
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("✓ Buffers created successfully\n");
    
    // Start processing
    result = asio->lpVtbl->start(asio);
    if (result != ASE_OK) {
        printf("Failed to start ASIO: %d\n", result);
        asio->lpVtbl->disposeBuffers(asio);
        asio->lpVtbl->Release(asio);
        CoUninitialize();
        return 1;
    }
    
    printf("✓ ASIO started - monitoring timing for 5 seconds...\n");
    
    // Monitor for 5 seconds
    Sleep(5000);
    
    // Stop processing
    asio->lpVtbl->stop(asio);
    asio->lpVtbl->disposeBuffers(asio);
    
    printf("\nTest Results:\n");
    printf("=============\n");
    printf("Total buffer switches: %d\n", buffer_switch_count);
    printf("Timing errors: %d\n", timing_errors);
    
    if (buffer_switch_count > 0) {
        double error_rate = (double)timing_errors / buffer_switch_count * 100.0;
        printf("Error rate: %.2f%%\n", error_rate);
        
        if (error_rate < 5.0) {
            printf("✅ EXCELLENT: Fixed buffer timing is working well!\n");
        } else if (error_rate < 15.0) {
            printf("✅ GOOD: Fixed buffer timing is mostly stable\n");
        } else {
            printf("⚠️ WARNING: High timing error rate - buffer size may not be truly fixed\n");
        }
    } else {
        printf("❌ FAILED: No buffer callbacks received\n");
    }
    
    // Cleanup
    asio->lpVtbl->Release(asio);
    CoUninitialize();
    
    return (timing_errors < buffer_switch_count * 0.15) ? 0 : 1;
} 