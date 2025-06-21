#include <stdio.h>
#include <windows.h>
#include <objbase.h>

// PipeWine CLSID - correct one from driver_clsid.h
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};

// ASIO interface (simplified)
typedef struct {
    HRESULT (__stdcall *QueryInterface)(void *this, REFIID riid, void **ppvObject);
    ULONG (__stdcall *AddRef)(void *this);
    ULONG (__stdcall *Release)(void *this);
    BOOL (__stdcall *Init)(void *this, void *sysRef);
    void (__stdcall *GetDriverName)(void *this, char *name);
    LONG (__stdcall *GetDriverVersion)(void *this);
    void (__stdcall *GetErrorMessage)(void *this, char *string);
    LONG (__stdcall *Start)(void *this);
    LONG (__stdcall *Stop)(void *this);
    LONG (__stdcall *GetChannels)(void *this, LONG *numInputChannels, LONG *numOutputChannels);
} IASIOVtbl;

typedef struct {
    IASIOVtbl *lpVtbl;
} IASIO;

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    
    printf("Testing PipeWine driver initialization...\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08lx\n", hr);
        return 1;
    }
    printf("COM initialized\n");
    
    // Create the ASIO driver instance
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, 
                         &CLSID_PipeWine, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create PipeWine instance: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("PipeWine instance created\n");
    
    // Try to initialize the driver
    printf("Calling Init()...\n");
    BOOL initResult = pASIO->lpVtbl->Init(pASIO, NULL);
    
    if (initResult) {
        printf("ASIO driver initialized successfully!\n");
        
        // Get driver name
        char driverName[256] = {0};
        pASIO->lpVtbl->GetDriverName(pASIO, driverName);
        printf("Driver name: %s\n", driverName);
        
        // Get channel count
        LONG inputs = 0, outputs = 0;
        LONG channelResult = pASIO->lpVtbl->GetChannels(pASIO, &inputs, &outputs);
        if (channelResult == 0) {
            printf("Channels: %ld inputs, %ld outputs\n", inputs, outputs);
        } else {
            printf("Failed to get channel information: %ld\n", channelResult);
        }
        
        printf("Sleeping for 5 seconds to observe behavior...\n");
        Sleep(5000);
        
    } else {
        printf("Failed to initialize ASIO driver\n");
        
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