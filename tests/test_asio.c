#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <windows.h>
#include <objbase.h>

// ASIO driver CLSID for PipeWine
static const CLSID CLSID_PipeWine = {0x48d0c522, 0xbfcc, 0x45cc, {0x8b, 0x84, 0x17, 0x6d, 0xa0, 0xb9, 0xe1, 0x05}};

// Minimal ASIO interface definition
typedef struct {
    void **lpVtbl;
} IUnknown;

typedef struct {
    void **lpVtbl;
} IASIO;

// Function pointers for ASIO methods
typedef int (__stdcall *ASIOInit_t)(IASIO *iface, void *sysRef);
typedef void (__stdcall *ASIOGetDriverName_t)(IASIO *iface, char *name);
typedef int (__stdcall *ASIOGetChannels_t)(IASIO *iface, long *numInputChannels, long *numOutputChannels);
typedef void (__stdcall *ASIOGetErrorMessage_t)(IASIO *iface, char *string);

int main() {
    HRESULT hr;
    IUnknown *pUnknown = NULL;
    IASIO *pASIO = NULL;
    
    printf("Testing PipeWine driver initialization...\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08x\n", (unsigned int)hr);
        return 1;
    }
    
    printf("COM initialized successfully\n");
    
    // Create the ASIO driver instance
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &IID_IUnknown, (void**)&pUnknown);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance: 0x%08x\n", (unsigned int)hr);
        CoUninitialize();
        return 1;
    }
    
    printf("PipeWine instance created successfully\n");
    
    // Query for ASIO interface
    hr = pUnknown->lpVtbl[0](pUnknown, &IID_IUnknown, (void**)&pASIO); // QueryInterface
    if (FAILED(hr)) {
        printf("Failed to query ASIO interface: 0x%08x\n", (unsigned int)hr);
        pUnknown->lpVtbl[2](pUnknown); // Release
        CoUninitialize();
        return 1;
    }
    
    printf("ASIO interface obtained successfully\n");
    
    // Try to initialize the driver
    ASIOInit_t ASIOInit = (ASIOInit_t)pASIO->lpVtbl[3]; // Assuming Init is at index 3
    int result = ASIOInit(pASIO, NULL);
    
    if (result) {
        printf("ASIO driver initialized successfully\n");
        
        // Get driver name
        ASIOGetDriverName_t ASIOGetDriverName = (ASIOGetDriverName_t)pASIO->lpVtbl[4];
        char driverName[256] = {0};
        ASIOGetDriverName(pASIO, driverName);
        printf("Driver name: %s\n", driverName);
        
        // Get channel count
        ASIOGetChannels_t ASIOGetChannels = (ASIOGetChannels_t)pASIO->lpVtbl[8];
        long inputs = 0, outputs = 0;
        int channelResult = ASIOGetChannels(pASIO, &inputs, &outputs);
        if (channelResult == 0) {
            printf("Channels: %ld inputs, %ld outputs\n", inputs, outputs);
        } else {
            printf("Failed to get channel information\n");
        }
        
        printf("Sleeping for 5 seconds to observe PipeWire graph...\n");
        sleep(5);
        
    } else {
        printf("Failed to initialize ASIO driver\n");
        
        // Try to get error message
        ASIOGetErrorMessage_t ASIOGetErrorMessage = (ASIOGetErrorMessage_t)pASIO->lpVtbl[6];
        char errorMsg[256] = {0};
        ASIOGetErrorMessage(pASIO, errorMsg);
        printf("Error message: %s\n", errorMsg);
    }
    
    // Cleanup
    pASIO->lpVtbl[2](pASIO); // Release
    pUnknown->lpVtbl[2](pUnknown); // Release
    CoUninitialize();
    
    printf("Test completed\n");
    return 0;
} 