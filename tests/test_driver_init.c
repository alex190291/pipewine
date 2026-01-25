#include <stdio.h>
#include <windows.h>
#include <objbase.h>

// PipeWine CLSID
const CLSID CLSID_WineASIO = {0xa4262ee4, 0xc528, 0x4ff9, {0x87, 0xbe, 0x56, 0x26, 0x1a, 0xd7, 0x92, 0xc3}};

// Minimal ASIO interface
typedef struct {
    void *vtbl;
} IASIO;

typedef long ASIOBool;
typedef long ASIOError;

int main(void) {
    HRESULT hr;
    IASIO *asio = NULL;

    printf("Initializing COM...\n");
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("ERROR: CoInitialize failed: 0x%08x\n", (unsigned int)hr);
        return 1;
    }

    printf("Creating PipeWine ASIO driver instance...\n");
    hr = CoCreateInstance(&CLSID_WineASIO, NULL, CLSCTX_INPROC_SERVER,
                          &CLSID_WineASIO, (void**)&asio);

    if (FAILED(hr)) {
        printf("ERROR: CoCreateInstance failed: 0x%08x\n", (unsigned int)hr);
        printf("This means the driver DLL couldn't be loaded or initialized.\n");

        switch (hr) {
            case REGDB_E_CLASSNOTREG:
                printf("  - REGDB_E_CLASSNOTREG: Driver not registered\n");
                break;
            case CLASS_E_NOAGGREGATION:
                printf("  - CLASS_E_NOAGGREGATION: Aggregation error\n");
                break;
            case E_NOINTERFACE:
                printf("  - E_NOINTERFACE: Interface not supported\n");
                break;
            default:
                printf("  - Unknown error code\n");
                break;
        }

        CoUninitialize();
        return 1;
    }

    printf("SUCCESS: Driver created successfully!\n");
    printf("Driver pointer: %p\n", asio);

    // Try to call init method (it's the second method in vtable typically)
    // This is where the actual initialization happens
    printf("\nNOTE: Driver object created, but actual Init() would be called by ASIO host.\n");
    printf("If Ableton fails here, check WINEDEBUG=+asio,+pipewire output.\n");

    if (asio) {
        asio->vtbl = NULL; // Don't actually call anything, just testing creation
    }

    CoUninitialize();
    printf("\nTest completed successfully.\n");
    return 0;
}
