#include <stdio.h>
#include <windows.h>

int main() {
    printf("=== Debug Test Starting ===\n");
    fflush(stdout);
    
    printf("1. Basic printf working\n");
    fflush(stdout);
    
    printf("2. About to initialize COM...\n");
    fflush(stdout);
    
    HRESULT hr = CoInitialize(NULL);
    printf("3. CoInitialize result: 0x%08lx\n", hr);
    fflush(stdout);
    
    if (SUCCEEDED(hr)) {
        printf("4. COM initialized successfully\n");
        fflush(stdout);
        
        CoUninitialize();
        printf("5. COM cleaned up\n");
        fflush(stdout);
    } else {
        printf("4. COM initialization failed\n");
        fflush(stdout);
    }
    
    printf("=== Debug Test Complete ===\n");
    fflush(stdout);
    
    return 0;
} 