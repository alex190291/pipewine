#include <stdio.h>
#include <windows.h>
#include <ole2.h>

// PipeWire ASIO CLSID
static const CLSID CLSID_PipeWine = {0xA4262EE4, 0xC528, 0x4FF9, {0x87, 0xBE, 0x56, 0x26, 0x1A, 0xD7, 0x92, 0xC3}};
static const IID IID_IASIO = {0x8B85C19A, 0x1B7A, 0x11D5, {0x9F, 0x85, 0x00, 0x60, 0x08, 0x3B, 0xF4, 0x3D}};

int main() {
    printf("=== Minimal PipeWire ASIO Driver Test ===\n");
    
    // Initialize COM
    printf("1. Initializing COM...\n");
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("❌ CoInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    printf("✓ COM initialized successfully\n");
    
    // Try to create the driver instance
    printf("2. Creating PipeWire ASIO driver instance...\n");
    void *asio = NULL;
    hr = CoCreateInstance(&CLSID_PipeWine, NULL, CLSCTX_INPROC_SERVER, &IID_IASIO, &asio);
    if (FAILED(hr)) {
        printf("❌ CoCreateInstance failed: 0x%08lx\n", hr);
        printf("   This means the driver is not properly registered or there's a loading issue\n");
        CoUninitialize();
        return 1;
    }
    printf("✓ Driver instance created successfully!\n");
    
    // Release the instance
    if (asio) {
        ((IUnknown*)asio)->lpVtbl->Release((IUnknown*)asio);
        printf("✓ Driver instance released\n");
    }
    
    // Clean up COM
    CoUninitialize();
    printf("✓ COM cleaned up\n");
    
    printf("\n🎉 SUCCESS: PipeWire ASIO driver can be instantiated!\n");
    printf("This confirms the driver is properly built and registered.\n");
    
    return 0;
} 