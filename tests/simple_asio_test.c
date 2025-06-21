#include <stdio.h>
#include <windows.h>
#include <ole2.h>

// ASIO driver CLSID
static const CLSID CLSID_WineASIO = {0x48d0c522, 0xbfcc, 0x4633, {0xa0, 0x99, 0x02, 0x80, 0x36, 0xb2, 0xfd, 0x33}};

// ASIO interface IID (simplified)
static const IID IID_ASIO = {0x48d0c522, 0xbfcc, 0x4633, {0xa0, 0x99, 0x02, 0x80, 0x36, 0xb2, 0xfd, 0x33}};

// Simplified ASIO interface
typedef struct {
    void *lpVtbl;
} IASIO;

typedef struct {
    // IUnknown methods
    HRESULT (*QueryInterface)(IASIO *this, REFIID riid, void **ppvObject);
    ULONG (*AddRef)(IASIO *this);
    ULONG (*Release)(IASIO *this);
    
    // ASIO methods
    int (*Init)(IASIO *this, void *sysRef);
    void (*GetDriverName)(IASIO *this, char *name);
    int (*GetDriverVersion)(IASIO *this);
} IASIOVtbl;

int main() {
    HRESULT hr;
    IASIO *pASIO = NULL;
    char driverName[256];
    
    printf("Simple ASIO driver test starting...\n");
    
    // Initialize COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("Failed to initialize COM: 0x%08x\n", hr);
        return 1;
    }
    printf("COM initialized successfully\n");
    
    // Try to create the ASIO driver instance
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER, &IID_ASIO, (void**)&pASIO);
    if (FAILED(hr)) {
        printf("Failed to create ASIO driver instance: 0x%08x\n", hr);
        CoUninitialize();
        return 1;
    }
    printf("ASIO driver instance created successfully!\n");
    
    // Try to get the driver name
    IASIOVtbl *vtbl = (IASIOVtbl*)pASIO->lpVtbl;
    vtbl->GetDriverName(pASIO, driverName);
    printf("Driver name: %s\n", driverName);
    
    // Release the instance
    if (pASIO) {
        vtbl->Release(pASIO);
        printf("ASIO driver instance released\n");
    }
    
    // Cleanup COM
    CoUninitialize();
    printf("Test completed successfully\n");
    
    return 0;
} 