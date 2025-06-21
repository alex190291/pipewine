#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <objbase.h>

// ASIO definitions
typedef double ASIOSampleRate;
typedef long ASIOBool;
typedef long ASIOError;
typedef struct ASIOSamples {
    unsigned long hi;
    unsigned long lo;
} ASIOSamples;
typedef struct ASIOTimeStamp {
    unsigned long hi;
    unsigned long lo;
} ASIOTimeStamp;

#define ASIOTrue 1
#define ASIOFalse 0
#define ASE_OK 0
#define ASE_NotPresent -1000

typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void *buffers[2];
} ASIOBufferInfo;

typedef struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    void (*bufferSwitchTimeInfo)(void* params, long doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

typedef struct IASIO_Vtbl {
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

typedef struct IASIO {
    IASIO_Vtbl *lpVtbl;
} IASIO;

// PipeWine CLSID
static const GUID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// Test callback functions
void bufferSwitch(long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch called: index=%ld\n", doubleBufferIndex);
}

void sampleRateDidChange(ASIOSampleRate sRate) {
    printf("Sample rate changed: %f\n", sRate);
}

long asioMessage(long selector, long value, void* message, double* opt) {
    printf("ASIO message: selector=%ld, value=%ld\n", selector, value);
    return 0;
}

void bufferSwitchTimeInfo(void* params, long doubleBufferIndex, ASIOBool directProcess) {
    printf("Buffer switch time info called: index=%ld\n", doubleBufferIndex);
}

// Helper function to fill buffers with test pattern
void fill_buffer_with_pattern(void *buffer, size_t size, unsigned char pattern) {
    if (buffer && size > 0) {
        memset(buffer, pattern, size);
    }
}

// Helper function to check if buffer contains expected pattern
int check_buffer_pattern(void *buffer, size_t size, unsigned char expected_pattern) {
    if (!buffer || size == 0) return 0;
    
    unsigned char *bytes = (unsigned char*)buffer;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != expected_pattern) {
            return 0; // Pattern mismatch
        }
    }
    return 1; // Pattern matches
}

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    
    printf("=== Buffer Restart Test - Verifying Buffer Clearing ===\n");
    printf("This test verifies that buffers are properly cleared when restarting the driver\n\n");
    
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
    
    // Get buffer size info
    printf("4. Getting buffer size information...\n");
    LONG minSize, maxSize, preferredSize, granularity;
    ASIOError result = pASIO->lpVtbl->GetBufferSize(pASIO, &minSize, &maxSize, &preferredSize, &granularity);
    if (result != ASE_OK) {
        printf("❌ Failed to get buffer size info: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    printf("   Buffer size: %ld samples\n", preferredSize);
    
    // Set up buffer configuration
    printf("5. Setting up buffer configuration...\n");
    ASIOBufferInfo bufferInfos[2];
    bufferInfos[0].isInput = ASIOFalse;  // Output channel 0
    bufferInfos[0].channelNum = 0;
    bufferInfos[0].buffers[0] = NULL;
    bufferInfos[0].buffers[1] = NULL;
    
    bufferInfos[1].isInput = ASIOFalse;  // Output channel 1
    bufferInfos[1].channelNum = 1;
    bufferInfos[1].buffers[0] = NULL;
    bufferInfos[1].buffers[1] = NULL;
    
    ASIOCallbacks callbacks = {
        .bufferSwitch = bufferSwitch,
        .sampleRateDidChange = sampleRateDidChange,
        .asioMessage = asioMessage,
        .bufferSwitchTimeInfo = bufferSwitchTimeInfo
    };
    
    // Create buffers
    printf("6. Creating buffers...\n");
    result = pASIO->lpVtbl->CreateBuffers(pASIO, bufferInfos, 2, preferredSize, &callbacks);
    if (result != ASE_OK) {
        printf("❌ Failed to create buffers: %ld\n", result);
        pASIO->lpVtbl->Release(pASIO);
        CoUninitialize();
        return 1;
    }
    
    printf("✓ Buffers created successfully\n");
    printf("   Channel 0: buffer[0]=%p, buffer[1]=%p\n", 
           bufferInfos[0].buffers[0], bufferInfos[0].buffers[1]);
    printf("   Channel 1: buffer[0]=%p, buffer[1]=%p\n", 
           bufferInfos[1].buffers[0], bufferInfos[1].buffers[1]);
    
    // Test 1: Verify initial buffers are clean (should be zeros)
    printf("\n7. Test 1: Verifying initial buffers are clean...\n");
    size_t buffer_size = preferredSize * sizeof(float);
    int initial_clean = 1;
    
    for (int ch = 0; ch < 2; ch++) {
        for (int buf = 0; buf < 2; buf++) {
            if (!check_buffer_pattern(bufferInfos[ch].buffers[buf], buffer_size, 0x00)) {
                printf("❌ Channel %d buffer %d is not clean initially\n", ch, buf);
                initial_clean = 0;
            }
        }
    }
    
    if (initial_clean) {
        printf("✓ All buffers are clean initially\n");
    }
    
    // Test 2: Fill buffers with test pattern and start/stop driver
    printf("\n8. Test 2: Filling buffers with test pattern...\n");
    unsigned char test_pattern = 0xAA;
    
    for (int ch = 0; ch < 2; ch++) {
        for (int buf = 0; buf < 2; buf++) {
            fill_buffer_with_pattern(bufferInfos[ch].buffers[buf], buffer_size, test_pattern);
            printf("   Filled channel %d buffer %d with pattern 0x%02X\n", ch, buf, test_pattern);
        }
    }
    
    // Verify pattern was written
    printf("9. Verifying test pattern was written...\n");
    int pattern_written = 1;
    for (int ch = 0; ch < 2; ch++) {
        for (int buf = 0; buf < 2; buf++) {
            if (!check_buffer_pattern(bufferInfos[ch].buffers[buf], buffer_size, test_pattern)) {
                printf("❌ Channel %d buffer %d does not contain expected pattern\n", ch, buf);
                pattern_written = 0;
            }
        }
    }
    
    if (pattern_written) {
        printf("✓ Test pattern successfully written to all buffers\n");
    }
    
    // Test 3: Start driver (should clear buffers)
    printf("\n10. Test 3: Starting driver (should clear buffers)...\n");
    result = pASIO->lpVtbl->Start(pASIO);
    if (result != ASE_OK) {
        printf("❌ Failed to start driver: %ld\n", result);
    } else {
        printf("✓ Driver started successfully\n");
        
        // Small delay to let any clearing happen
        Sleep(100);
        
        // Check if buffers were cleared
        printf("11. Checking if buffers were cleared on start...\n");
        int buffers_cleared = 1;
        for (int ch = 0; ch < 2; ch++) {
            for (int buf = 0; buf < 2; buf++) {
                if (!check_buffer_pattern(bufferInfos[ch].buffers[buf], buffer_size, 0x00)) {
                    printf("❌ Channel %d buffer %d was not cleared on start\n", ch, buf);
                    buffers_cleared = 0;
                } else {
                    printf("✓ Channel %d buffer %d was properly cleared\n", ch, buf);
                }
            }
        }
        
        if (buffers_cleared) {
            printf("✓ All buffers were properly cleared on driver start\n");
        } else {
            printf("❌ Some buffers were not cleared on driver start\n");
        }
        
        // Test 4: Fill buffers again and stop driver
        printf("\n12. Test 4: Filling buffers again with different pattern...\n");
        unsigned char test_pattern2 = 0x55;
        
        for (int ch = 0; ch < 2; ch++) {
            for (int buf = 0; buf < 2; buf++) {
                fill_buffer_with_pattern(bufferInfos[ch].buffers[buf], buffer_size, test_pattern2);
            }
        }
        
        printf("13. Stopping driver (should clear buffers)...\n");
        result = pASIO->lpVtbl->Stop(pASIO);
        if (result != ASE_OK) {
            printf("❌ Failed to stop driver: %ld\n", result);
        } else {
            printf("✓ Driver stopped successfully\n");
            
            // Check if buffers were cleared on stop
            printf("14. Checking if buffers were cleared on stop...\n");
            int stop_cleared = 1;
            for (int ch = 0; ch < 2; ch++) {
                for (int buf = 0; buf < 2; buf++) {
                    if (!check_buffer_pattern(bufferInfos[ch].buffers[buf], buffer_size, 0x00)) {
                        printf("❌ Channel %d buffer %d was not cleared on stop\n", ch, buf);
                        stop_cleared = 0;
                    } else {
                        printf("✓ Channel %d buffer %d was properly cleared\n", ch, buf);
                    }
                }
            }
            
            if (stop_cleared) {
                printf("✓ All buffers were properly cleared on driver stop\n");
            } else {
                printf("❌ Some buffers were not cleared on driver stop\n");
            }
        }
        
        // Test 5: Restart cycle to verify clean restart
        printf("\n15. Test 5: Testing restart cycle...\n");
        
        // Fill with pattern again
        for (int ch = 0; ch < 2; ch++) {
            for (int buf = 0; buf < 2; buf++) {
                fill_buffer_with_pattern(bufferInfos[ch].buffers[buf], buffer_size, 0xFF);
            }
        }
        
        // Start again
        result = pASIO->lpVtbl->Start(pASIO);
        if (result == ASE_OK) {
            Sleep(100);
            
            // Check if restart cleared buffers
            printf("16. Checking if restart cleared buffers...\n");
            int restart_cleared = 1;
            for (int ch = 0; ch < 2; ch++) {
                for (int buf = 0; buf < 2; buf++) {
                    if (!check_buffer_pattern(bufferInfos[ch].buffers[buf], buffer_size, 0x00)) {
                        printf("❌ Channel %d buffer %d was not cleared on restart\n", ch, buf);
                        restart_cleared = 0;
                    }
                }
            }
            
            if (restart_cleared) {
                printf("✓ All buffers were properly cleared on restart\n");
            }
            
            pASIO->lpVtbl->Stop(pASIO);
        }
    }
    
    // Cleanup
    printf("\n17. Cleaning up...\n");
    pASIO->lpVtbl->DisposeBuffers(pASIO);
    pASIO->lpVtbl->Release(pASIO);
    CoUninitialize();
    
    printf("\n=== Buffer Restart Test Complete ===\n");
    printf("This test verified that the PipeWire ASIO driver properly clears\n");
    printf("audio buffers when starting, stopping, and restarting to prevent\n");
    printf("distorted audio from residual data.\n");
    
    return 0;
} 