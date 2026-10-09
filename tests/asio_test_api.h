#ifndef PIPEWINE_ASIO_TEST_API_H
#define PIPEWINE_ASIO_TEST_API_H
#include <windows.h>
#include <objbase.h>
#include "../driver_clsid.h"
#define IEEE754_64FLOAT 1
#undef NATIVE_INT64
#include <asio.h>

/* Match Windows ASIO's 32-bit LONG and callback calling convention even
 * when winegcc is hosted on an LP64 Unix system. */
typedef struct IASIO IASIO;
typedef struct IASIOVtbl {
    HRESULT (__stdcall *QueryInterface)(IASIO *, REFIID, void **);
    ULONG (__stdcall *AddRef)(IASIO *);
    ULONG (__stdcall *Release)(IASIO *);
    ASIOBool (__stdcall *Init)(IASIO *, void *);
    void (__stdcall *GetDriverName)(IASIO *, char *);
    LONG (__stdcall *GetDriverVersion)(IASIO *);
    void (__stdcall *GetErrorMessage)(IASIO *, char *);
    ASIOError (__stdcall *Start)(IASIO *);
    ASIOError (__stdcall *Stop)(IASIO *);
    ASIOError (__stdcall *GetChannels)(IASIO *, LONG *, LONG *);
    ASIOError (__stdcall *GetLatencies)(IASIO *, LONG *, LONG *);
    ASIOError (__stdcall *GetBufferSize)(IASIO *, LONG *, LONG *, LONG *, LONG *);
    ASIOError (__stdcall *CanSampleRate)(IASIO *, ASIOSampleRate);
    ASIOError (__stdcall *GetSampleRate)(IASIO *, ASIOSampleRate *);
    ASIOError (__stdcall *SetSampleRate)(IASIO *, ASIOSampleRate);
    ASIOError (__stdcall *GetClockSources)(IASIO *, ASIOClockSource *, LONG *);
    ASIOError (__stdcall *SetClockSource)(IASIO *, LONG);
    ASIOError (__stdcall *GetSamplePosition)(IASIO *, ASIOSamples *, ASIOTimeStamp *);
    ASIOError (__stdcall *GetChannelInfo)(IASIO *, ASIOChannelInfo *);
    ASIOError (__stdcall *CreateBuffers)(IASIO *, ASIOBufferInfo *, LONG, LONG, ASIOCallbacks *);
    ASIOError (__stdcall *DisposeBuffers)(IASIO *);
    ASIOError (__stdcall *ControlPanel)(IASIO *);
    ASIOError (__stdcall *Future)(IASIO *, LONG, void *);
    ASIOError (__stdcall *OutputReady)(IASIO *);
} IASIOVtbl;
struct IASIO { const IASIOVtbl *lpVtbl; };
#endif
