#include <stdio.h>
#include <windows.h>
#include <ole2.h>

// PipeWire ASIO CLSID
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};
static const IID IID_IASIO = {0x8B85C19A, 0x1B7A, 0x11D5, {0x9F, 0x85, 0x00, 0x60, 0x08, 0x3B, 0xF4, 0x3D}};

int main() {
    printf("=== Minimal COM Test ===\n");
    
    // Initialize COM
    printf("1. Initializing COM...\n");
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("❌ CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("✓ COM initialized\n");
    
    // Try to create ASIO driver instance
    printf("2. Creating PipeWire ASIO driver instance...\n");
    void *asio = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, &IID_IASIO, &asio);
    if (FAILED(hr)) {
        printf("❌ CoCreateInstance failed: 0x%08lx\n", hr);
        printf("   This suggests the driver is not properly registered or there's a DLL loading issue\n");
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver instance created successfully: %p\n", asio);
    
    // Release and cleanup
    printf("3. Cleaning up...\n");
    if (asio) {
        ((IUnknown*)asio)->lpVtbl->Release((IUnknown*)asio);
    }
    CoUninitialize();
    
    printf("✓ Minimal COM test completed successfully!\n");
    return 0;
} 