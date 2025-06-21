#include <stdio.h>
#include <windows.h>

int main() {
    printf("Simple DLL load test\n");
    fflush(stdout);
    
    HMODULE hDll = LoadLibrary("pipewine64.dll");
    if (hDll == NULL) {
        printf("Failed to load pipewine64.dll: %lu\n", GetLastError());
        fflush(stdout);
        return 1;
    }
    
    printf("Successfully loaded pipewine64.dll\n");
    fflush(stdout);
    
    FreeLibrary(hDll);
    printf("Test completed\n");
    fflush(stdout);
    
    return 0;
} 