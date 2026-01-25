/*
 * Copyright (C) 2006 Robert Reif
 * Portions copyright (C) 2007 Ralf Beck
 * Portions copyright (C) 2007 Johnny Petrantoni
 * Portions copyright (C) 2007 Stephane Letz
 * Portions copyright (C) 2008 William Steidtmann
 * Portions copyright (C) 2010 Peter L Jones
 * Portions copyright (C) 2010 Torben Hohn
 * Portions copyright (C) 2010 Nedko Arnaudov
 * Portions copyright (C) 2013 Joakim Hernberg
 * Portions copyright (C) 2020-2024 Filipe Coelho
 * Portions copyright (C) 2024-2025 Dawid Kraiński
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdatomic.h>

#include <jack/jack.h>
#include <jack/thread.h>

#include <spa/param/audio/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/param/latency-utils.h>
#include <spa/control/control.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/buffer/buffer.h>
#include <pipewire/buffers.h>
#include <pipewire/context.h>
#include <pipewire/filter.h>
#include <pipewire/keys.h>

#include "gui/gui_stub.inc.c"
#include "pw_helper_c.h"
#include "pw_helper_common.h"
#include "pw_midi.h"
#include "pw_asio_midi.h"
#include "driver_clsid.h"

/* Performance optimization macros */
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#define CACHE_LINE_SIZE 64
#define ALIGN_TO_CACHE_LINE(size) (((size) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))

#ifdef DEBUG
#include <wine/debug.h>
#else
#define TRACE(...) {}
#define WARN(fmt, ...) fprintf(stdout, "WARN: " fmt, ##__VA_ARGS__)
#define ERR(fmt, ...) fprintf(stderr, "ERR: " fmt, ##__VA_ARGS__)
#endif

#include <objbase.h>
#include <mmsystem.h>
#include <winreg.h>
#ifdef WINE_WITH_UNICODE
#include <wine/unicode.h>
#endif

#define IEEE754_64FLOAT 1
#undef NATIVE_INT64
#include <asio.h>

#ifdef DEBUG
WINE_DEFAULT_DEBUG_CHANNEL(asio);
#endif

#define MAX_ENVIRONMENT_SIZE        6
#define ASIO_MAX_NAME_LENGTH        32
#define ASIO_MINIMUM_BUFFERSIZE     16
#define ASIO_MAXIMUM_BUFFERSIZE     8192
#define ASIO_PREFERRED_BUFFERSIZE   1024
#define PW_ASIO_MAX_CHANNELS        64
#define PW_ASIO_DEFAULT_MIDI_INPUTS 1
#define PW_ASIO_DEFAULT_MIDI_OUTPUTS 1
#define PW_ASIO_MAX_MIDI_PORTS      1
#define PW_ASIO_MIDI_BUFFER_BYTES   (MAX_MIDI_EVENTS_PER_BUFFER * 32u)

#define ASIO_LONG(typ, x) ({ uint64_t __long_val = (x); (typ) { .lo = (uint32_t)__long_val, .hi = (uint32_t)(__long_val >> 32) }; })

/* ASIO drivers (breaking the COM specification) use the Microsoft variety of
 * thiscall calling convention which gcc is unable to produce.  These macros
 * add an extra layer to fixup the registers. Borrowed from config.h and the
 * wine source code.
 */

/* From config.h */
#define __ASM_DEFINE_FUNC(name,suffix,code) asm(".text\n\t.align 4\n\t.globl " #name suffix "\n\t.type " #name suffix ",@function\n" #name suffix ":\n\t.cfi_startproc\n\t" code "\n\t.cfi_endproc\n\t.previous");
#define __ASM_GLOBAL_FUNC(name,code) __ASM_DEFINE_FUNC(name,"",code)
#define __ASM_NAME(name) name
#define __ASM_STDCALL(args) ""

/* From wine source */
#ifdef __i386__  /* thiscall functions are i386-specific */

#define THISCALL(func) __thiscall_ ## func
#define THISCALL_NAME(func) __ASM_NAME("__thiscall_" #func)
#define __thiscall __stdcall
#define DEFINE_THISCALL_WRAPPER(func,args) \
    extern void THISCALL(func)(void); \
    __ASM_GLOBAL_FUNC(__thiscall_ ## func, \
                      "popl %eax\n\t" \
                      "pushl %ecx\n\t" \
                      "pushl %eax\n\t" \
                      "jmp " __ASM_NAME(#func) __ASM_STDCALL(args) )
#else /* __i386__ */

#define THISCALL(func) func
#define THISCALL_NAME(func) __ASM_NAME(#func)
#define __thiscall __stdcall
#define DEFINE_THISCALL_WRAPPER(func,args) /* nothing */

#endif /* __i386__ */

/* Hide ELF symbols for the COM members - No need to to export them */
#define HIDDEN __attribute__ ((visibility("hidden")))

/*****************************************************************************
 * IWineAsio interface
 */

#define INTERFACE IWineASIO
DECLARE_INTERFACE_(IWineASIO,IUnknown)
{
    STDMETHOD_(HRESULT, QueryInterface)         (THIS_ IID riid, void** ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)                   (THIS) PURE;
    STDMETHOD_(ULONG, Release)                  (THIS) PURE;
    STDMETHOD_(ASIOBool, Init)                  (THIS_ void *sysRef) PURE;
    STDMETHOD_(void, GetDriverName)             (THIS_ char *name) PURE;
    STDMETHOD_(LONG, GetDriverVersion)          (THIS) PURE;
    STDMETHOD_(void, GetErrorMessage)           (THIS_ char *string) PURE;
    STDMETHOD_(ASIOError, Start)                (THIS) PURE;
    STDMETHOD_(ASIOError, Stop)                 (THIS) PURE;
    STDMETHOD_(ASIOError, GetChannels)          (THIS_ LONG *numInputChannels, LONG *numOutputChannels) PURE;
    STDMETHOD_(ASIOError, GetLatencies)         (THIS_ LONG *inputLatency, LONG *outputLatency) PURE;
    STDMETHOD_(ASIOError, GetBufferSize)        (THIS_ LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity) PURE;
    STDMETHOD_(ASIOError, CanSampleRate)        (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, GetSampleRate)        (THIS_ ASIOSampleRate *sampleRate) PURE;
    STDMETHOD_(ASIOError, SetSampleRate)        (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, GetClockSources)      (THIS_ ASIOClockSource *clocks, LONG *numSources) PURE;
    STDMETHOD_(ASIOError, SetClockSource)       (THIS_ LONG index) PURE;
    STDMETHOD_(ASIOError, GetSamplePosition)    (THIS_ ASIOSamples *sPos, ASIOTimeStamp *tStamp) PURE;
    STDMETHOD_(ASIOError, GetChannelInfo)       (THIS_ ASIOChannelInfo *info) PURE;
    STDMETHOD_(ASIOError, CreateBuffers)        (THIS_ ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks) PURE;
    STDMETHOD_(ASIOError, DisposeBuffers)       (THIS) PURE;
    STDMETHOD_(ASIOError, ControlPanel)         (THIS) PURE;
    STDMETHOD_(ASIOError, Future)               (THIS_ LONG selector,void *opt) PURE;
    STDMETHOD_(ASIOError, OutputReady)          (THIS) PURE;
};
#undef INTERFACE

typedef struct IWineASIO *LPWINEASIO;

typedef struct IOChannel
{
    bool                         active;
    char                         port_name[ASIO_MAX_NAME_LENGTH];
    void                        *port;
    struct pw_buffer            *buffers[2];
    
    /* Wine-compatible buffer management */
    void                        *wine_buffers[2];  /* Wine-allocated buffers */
    size_t                       buffer_size;      /* Size of each buffer in bytes */
    bool                         needs_copy;       /* Whether data copying is needed */
} IOChannel;

typedef struct MidiPort
{
    bool                         active;
    char                         port_name[ASIO_MAX_NAME_LENGTH];
    void                        *port;
} MidiPort;

#define DEVICE_NAME_SIZE 1024

typedef struct IWineASIOImpl
{
    /* COM stuff */
    const IWineASIOVtbl         *lpVtbl;
    LONG                        ref;

    /* Reference to the DLL class factory (to keep DLL alive while an object is live) */
    IUnknown                   *cls_factory;

    /* The app's main window handle on windows, 0 on OS/X */
    HWND                        sys_ref;

    /* ASIO stuff */
    LONG                        asio_active_inputs;
    LONG                        asio_active_outputs;
    bool                        asio_buffer_index;
    ASIOCallbacks               *asio_callbacks;
    LONG                        asio_current_buffersize;
    INT                         asio_driver_state;
    uint64_t                    asio_sample_position;
    double                      asio_sample_rate;
    ASIOTime                    asio_time;
    uint64_t                    asio_time_stamp;
    LONG                        asio_version;
    bool                        asio_can_time_code;
    bool                        asio_time_info_mode;

    /* WineASIO configuration options */
    bool                        wineasio_connect_to_hardware;
    bool                        wineasio_fixed_buffersize;
    int                         wineasio_number_inputs;
    int                         wineasio_number_outputs;
    int                         wineasio_number_midi_inputs;
    int                         wineasio_number_midi_outputs;
    LONG                        wineasio_preferred_buffersize;
    WCHAR                       pwasio_input_device_name[DEVICE_NAME_SIZE];
    WCHAR                       pwasio_output_device_name[DEVICE_NAME_SIZE];

    /* PipeWire stuff */
    struct user_pw_helper *pw_helper;
    struct pw_loop *pw_loop;
    struct pw_context *pw_context;
    struct pw_core *pw_core;

    struct pw_node *current_input_node;
    struct pw_node *current_output_node;

    HANDLE                     node_resolve_thread;
    LONG                       node_resolve_stop;

    struct pw_filter *pw_filter;
    struct spa_hook pw_filter_listener;

    struct pwasio_gui *gui;
    struct pwasio_gui_conf gui_conf;

    char                        client_name[ASIO_MAX_NAME_LENGTH];

    /* Audio channel buffers */
    IOChannel                   *input_channel;
    IOChannel                   *output_channel;

    /* MIDI ports and buffers */
    MidiPort                    *midi_input_ports;
    MidiPort                    *midi_output_ports;
    struct midi_buffer          midi_in_buffer;
    struct midi_buffer          midi_out_buffer;
    bool                        midi_buffers_initialized;
    uint32_t                    midi_buffer_bytes;

    uint32_t                     asio_buffers_left_to_init;
    pthread_barrier_t            asio_buffers_filled;
} IWineASIOImpl;

enum { Loaded, Initialized, Prepared, Running };

/****************************************************************************
 *  Interface Methods
 */

/*
 *  as seen from the WineASIO source
 */

HIDDEN HRESULT   STDMETHODCALLTYPE      QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject);
HIDDEN ULONG     STDMETHODCALLTYPE      AddRef(LPWINEASIO iface);
HIDDEN ULONG     STDMETHODCALLTYPE      Release(LPWINEASIO iface);
HIDDEN ASIOBool  STDMETHODCALLTYPE      Init(LPWINEASIO iface, void *sysRef);
HIDDEN void      STDMETHODCALLTYPE      GetDriverName(LPWINEASIO iface, char *name);
HIDDEN LONG      STDMETHODCALLTYPE      GetDriverVersion(LPWINEASIO iface);
HIDDEN void      STDMETHODCALLTYPE      GetErrorMessage(LPWINEASIO iface, char *string);
HIDDEN ASIOError STDMETHODCALLTYPE      Start(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      Stop(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels);
HIDDEN ASIOError STDMETHODCALLTYPE      GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency);
HIDDEN ASIOError STDMETHODCALLTYPE      GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
HIDDEN ASIOError STDMETHODCALLTYPE      CanSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      GetSampleRate(LPWINEASIO iface, ASIOSampleRate *sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      SetSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE      GetClockSources(LPWINEASIO iface, ASIOClockSource *clocks, LONG *numSources);
HIDDEN ASIOError STDMETHODCALLTYPE      SetClockSource(LPWINEASIO iface, LONG index);
HIDDEN ASIOError STDMETHODCALLTYPE      GetSamplePosition(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp);
HIDDEN ASIOError STDMETHODCALLTYPE      GetChannelInfo(LPWINEASIO iface, ASIOChannelInfo *info);
HIDDEN ASIOError STDMETHODCALLTYPE      CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks);
HIDDEN ASIOError STDMETHODCALLTYPE      DisposeBuffers(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      ControlPanel(LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE      Future(LPWINEASIO iface, LONG selector, void *opt);
HIDDEN ASIOError STDMETHODCALLTYPE      OutputReady(LPWINEASIO iface);

/*
 * thiscall wrappers for the vtbl (as seen from app side 32bit)
 */

HIDDEN void __thiscall_Init(void);
HIDDEN void __thiscall_GetDriverName(void);
HIDDEN void __thiscall_GetDriverVersion(void);
HIDDEN void __thiscall_GetErrorMessage(void);
HIDDEN void __thiscall_Start(void);
HIDDEN void __thiscall_Stop(void);
HIDDEN void __thiscall_GetChannels(void);
HIDDEN void __thiscall_GetLatencies(void);
HIDDEN void __thiscall_GetBufferSize(void);
HIDDEN void __thiscall_CanSampleRate(void);
HIDDEN void __thiscall_GetSampleRate(void);
HIDDEN void __thiscall_SetSampleRate(void);
HIDDEN void __thiscall_GetClockSources(void);
HIDDEN void __thiscall_SetClockSource(void);
HIDDEN void __thiscall_GetSamplePosition(void);
HIDDEN void __thiscall_GetChannelInfo(void);
HIDDEN void __thiscall_CreateBuffers(void);
HIDDEN void __thiscall_DisposeBuffers(void);
HIDDEN void __thiscall_ControlPanel(void);
HIDDEN void __thiscall_Future(void);
HIDDEN void __thiscall_OutputReady(void);

/*
 *  PipeWire callbacks
 */

/*
 *  Support functions
 */

HRESULT WINAPI  WineASIOCreateInstance(REFIID riid, LPVOID *ppobj, IUnknown *cls_factory);
static  void    configure_driver(IWineASIOImpl *This);
static  void    get_nodes_by_name(IWineASIOImpl *This);
static  void    Uninit(IWineASIOImpl *This);

HIDDEN void GuiClosed(struct pwasio_gui_conf *conf);
HIDDEN void GuiApplyConfig(struct pwasio_gui_conf *conf);
HIDDEN void GuiLoadConfig(struct pwasio_gui_conf *conf);

static DWORD WINAPI jack_thread_creator_helper(LPVOID arg);
static int          jack_thread_creator(pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg);
static DWORD WINAPI node_resolve_thread_proc(LPVOID arg);
static void         ensure_nodes_ready_async(IWineASIOImpl *This);

static const IWineASIOVtbl WineASIO_Vtbl =
{
    (void *) QueryInterface,
    (void *) AddRef,
    (void *) Release,

    (void *) THISCALL(Init),
    (void *) THISCALL(GetDriverName),
    (void *) THISCALL(GetDriverVersion),
    (void *) THISCALL(GetErrorMessage),
    (void *) THISCALL(Start),
    (void *) THISCALL(Stop),
    (void *) THISCALL(GetChannels),
    (void *) THISCALL(GetLatencies),
    (void *) THISCALL(GetBufferSize),
    (void *) THISCALL(CanSampleRate),
    (void *) THISCALL(GetSampleRate),
    (void *) THISCALL(SetSampleRate),
    (void *) THISCALL(GetClockSources),
    (void *) THISCALL(SetClockSource),
    (void *) THISCALL(GetSamplePosition),
    (void *) THISCALL(GetChannelInfo),
    (void *) THISCALL(CreateBuffers),
    (void *) THISCALL(DisposeBuffers),
    (void *) THISCALL(ControlPanel),
    (void *) THISCALL(Future),
    (void *) THISCALL(OutputReady)
};

/* structure needed to create the PipeWire callback thread in the wine process context */
struct {
    void        *(*jack_callback_thread) (void*);
    void        *arg;
    pthread_t   jack_callback_pthread_id;
    HANDLE      jack_callback_thread_created;
} jack_thread_creator_privates;

/* ASIO callback marshalling system for Wine thread context */
typedef struct {
    IWineASIOImpl *This;
    LONG buffer_index;
    ASIOBool direct_process;
    ASIOTime asio_time;
    bool use_time_info;
    HANDLE callback_event;
    HANDLE callback_completed;
    volatile bool callback_pending;
    volatile bool thread_should_exit;
} ASIOCallbackData;

typedef struct {
    HANDLE callback_thread;
    DWORD callback_thread_id;
    ASIOCallbackData callback_data;
    CRITICAL_SECTION callback_lock;
} ASIOCallbackManager;

static ASIOCallbackManager g_callback_manager = {0};

static char g_last_error[256] = "No error";

static DWORD WINAPI node_resolve_thread_proc(LPVOID arg) {
    IWineASIOImpl *This = (IWineASIOImpl *)arg;
    int backoff_ms = 10;

    while (!InterlockedCompareExchange(&This->node_resolve_stop, 0, 0)) {
        get_nodes_by_name(This);

        if (This->current_input_node && This->current_output_node) {
            TRACE("Async node resolve completed\n");
            break;
        }

        user_pw_trigger_event_processing(This->pw_helper);
        Sleep(backoff_ms);
        if (backoff_ms < 200) {
            backoff_ms *= 2;
        }
    }

    return 0;
}

static void ensure_nodes_ready_async(IWineASIOImpl *This) {
    HANDLE thread = This->node_resolve_thread;
    DWORD exit_code = 0;

    if (thread) {
        if (GetExitCodeThread(thread, &exit_code) && exit_code == STILL_ACTIVE) {
            return;
        }
        CloseHandle(thread);
        This->node_resolve_thread = NULL;
    }

    This->node_resolve_stop = 0;
    This->node_resolve_thread = CreateThread(NULL, 0, node_resolve_thread_proc, This, 0, NULL);
    if (!This->node_resolve_thread) {
        WARN("Failed to start node resolve thread\n");
    }
}

static void set_last_error(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

/* Wine thread function for ASIO callbacks */
static DWORD WINAPI asio_callback_thread_proc(LPVOID param) {
    ASIOCallbackManager *manager = (ASIOCallbackManager*)param;
    ASIOCallbackData *data = &manager->callback_data;

    TRACE("ASIO callback thread started\n");

    while (!data->thread_should_exit) {
        /* Simple wait for callback request from PipeWire thread
         * Following WineASIO's approach: NO message processing in callback thread
         * Wine's main thread handles all Windows messages including MIDI input
         */
        DWORD wait_result = WaitForSingleObject(data->callback_event, 1000); /* 1 second timeout */

        if (wait_result == WAIT_TIMEOUT) {
            continue; /* Check exit condition */
        }

        if (wait_result != WAIT_OBJECT_0 || data->thread_should_exit) {
            break;
        }
        
        /* Process the callback in Wine's thread context */
        EnterCriticalSection(&manager->callback_lock);

        if (data->callback_pending && data->This && data->This->asio_callbacks) {
            IWineASIOImpl *This = data->This;

            TRACE("Executing ASIO callback in Wine thread context\n");

            /* Measure callback execution time for debugging slow callbacks */
            LARGE_INTEGER start_time, end_time, frequency;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start_time);

            /* Call the ASIO callback in Wine's thread context */
            if (data->use_time_info && This->asio_time_info_mode) {
                This->asio_callbacks->bufferSwitchTimeInfo(&data->asio_time, data->buffer_index, data->direct_process);
            } else {
                This->asio_callbacks->bufferSwitch(data->buffer_index, data->direct_process);
            }

            QueryPerformanceCounter(&end_time);
            double callback_ms = ((end_time.QuadPart - start_time.QuadPart) * 1000.0) / frequency.QuadPart;

            /* Warn if callback takes longer than quantum duration (indicates host app is slow) */
            static int slow_callback_count = 0;
            double quantum_ms = (This->asio_current_buffersize * 1000.0) / This->asio_sample_rate;
            if (callback_ms > quantum_ms && slow_callback_count < 3) {
                printf("WARNING: ASIO callback took %.2f ms (quantum=%.2f ms) - host app processing is slow!\n",
                       callback_ms, quantum_ms);
                printf("  This may cause audio glitches. Consider increasing buffer size.\n");
                slow_callback_count++;
            }

            data->callback_pending = false;
        }

        LeaveCriticalSection(&manager->callback_lock);

        /* Signal completion to PipeWire thread */
        SetEvent(data->callback_completed);

        /* No sleep or yield needed - following WineASIO's simple approach
         * Wine's main thread handles all MIDI input
         * Normal thread priority prevents starvation of Wine's MIDI processing
         */
    }
    
    TRACE("ASIO callback thread exiting\n");
    return 0;
}

/* Initialize the ASIO callback manager */
static BOOL init_asio_callback_manager(IWineASIOImpl *This) {
    if (g_callback_manager.callback_thread) {
        return TRUE; /* Already initialized */
    }
    
    TRACE("Initializing ASIO callback manager\n");
    
    InitializeCriticalSection(&g_callback_manager.callback_lock);
    
    /* Initialize callback data */
    g_callback_manager.callback_data.This = This;
    g_callback_manager.callback_data.callback_pending = false;
    g_callback_manager.callback_data.thread_should_exit = false;
    
    /* Create synchronization events */
    g_callback_manager.callback_data.callback_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_callback_manager.callback_data.callback_completed = CreateEventW(NULL, FALSE, FALSE, NULL);
    
    if (!g_callback_manager.callback_data.callback_event || 
        !g_callback_manager.callback_data.callback_completed) {
        ERR("Failed to create callback synchronization events\n");
        set_last_error("Failed to create callback synchronization events");
        return FALSE;
    }
    
    /* Create the Wine callback thread */
    g_callback_manager.callback_thread = CreateThread(
        NULL, 0, asio_callback_thread_proc, &g_callback_manager, 0, 
        &g_callback_manager.callback_thread_id);
    
    if (!g_callback_manager.callback_thread) {
        ERR("Failed to create ASIO callback thread\n");
        CloseHandle(g_callback_manager.callback_data.callback_event);
        CloseHandle(g_callback_manager.callback_data.callback_completed);
        DeleteCriticalSection(&g_callback_manager.callback_lock);
        return FALSE;
    }
    
    /* Set thread priority for low-latency audio
     * Critical finding: MIDI stops working when thread priority is too high
     * Testing showed:
     *   - TIME_CRITICAL: MIDI stops after 1-2 minutes
     *   - HIGHEST: MIDI stops after 1-2 minutes
     *   - ABOVE_NORMAL: Still causes MIDI issues
     * Following WineASIO's simpler approach: DON'T elevate priority at all
     * Let Windows scheduler handle it - this prevents Wine's MIDI thread from being starved
     * If you get audio glitches, increase buffer size instead of thread priority
     */
    SetThreadPriority(g_callback_manager.callback_thread, THREAD_PRIORITY_NORMAL);

    /* CPU affinity pinning disabled - it can cause thread starvation issues
     * The OS scheduler does a good job of keeping threads on the same core anyway.
     * If cache performance becomes an issue, this can be re-enabled via config.
     */
    #if 0
    {
        DWORD_PTR process_affinity, system_affinity;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity, &system_affinity)) {
            /* Pin to first available CPU core for consistent performance */
            DWORD_PTR thread_affinity = process_affinity & (-(LONG_PTR)process_affinity);
            SetThreadAffinityMask(g_callback_manager.callback_thread, thread_affinity);
        }
    }
    #endif
    
    TRACE("ASIO callback manager initialized successfully\n");
    /* Verbose debug disabled for cleaner output */
    /* printf("ASIO callback manager initialized: thread_id=%lu\n", g_callback_manager.callback_thread_id); */
    return TRUE;
}

/* Cleanup the ASIO callback manager */
static void cleanup_asio_callback_manager(void) {
    if (!g_callback_manager.callback_thread) {
        return; /* Not initialized */
    }
    
    TRACE("Cleaning up ASIO callback manager\n");
    
    /* Signal thread to exit */
    EnterCriticalSection(&g_callback_manager.callback_lock);
    g_callback_manager.callback_data.thread_should_exit = true;
    LeaveCriticalSection(&g_callback_manager.callback_lock);
    
    /* Wake up the thread */
    SetEvent(g_callback_manager.callback_data.callback_event);
    
    /* Wait for thread to exit */
    WaitForSingleObject(g_callback_manager.callback_thread, 5000);
    
    /* Cleanup resources */
    CloseHandle(g_callback_manager.callback_thread);
    CloseHandle(g_callback_manager.callback_data.callback_event);
    CloseHandle(g_callback_manager.callback_data.callback_completed);
    DeleteCriticalSection(&g_callback_manager.callback_lock);
    
    /* Reset manager */
    memset(&g_callback_manager, 0, sizeof(g_callback_manager));
    
    TRACE("ASIO callback manager cleaned up\n");
}

/* Marshal ASIO callback from PipeWire thread to Wine thread */
static void marshal_asio_callback(IWineASIOImpl *This, LONG buffer_index, ASIOBool direct_process, 
                                  ASIOTime *asio_time, bool use_time_info) {
    if (!g_callback_manager.callback_thread) {
        ERR("ASIO callback manager not initialized\n");
        set_last_error("ASIO callback manager not initialized");
        return;
    }
    
    /* Prepare callback data */
    EnterCriticalSection(&g_callback_manager.callback_lock);
    
    if (g_callback_manager.callback_data.callback_pending) {
        /* Previous callback still pending - this shouldn't happen in normal operation */
        WARN("Previous ASIO callback still pending, skipping\n");
        set_last_error("Previous ASIO callback still pending");
        LeaveCriticalSection(&g_callback_manager.callback_lock);
        return;
    }
    
    g_callback_manager.callback_data.This = This;
    g_callback_manager.callback_data.buffer_index = buffer_index;
    g_callback_manager.callback_data.direct_process = direct_process;
    g_callback_manager.callback_data.use_time_info = use_time_info;
    
    if (use_time_info && asio_time) {
        g_callback_manager.callback_data.asio_time = *asio_time;
    }
    
    g_callback_manager.callback_data.callback_pending = true;
    
    LeaveCriticalSection(&g_callback_manager.callback_lock);
    
    /* Signal the Wine thread to process the callback */
    SetEvent(g_callback_manager.callback_data.callback_event);

    /* Wait for callback completion with dynamic timeout
     * Timeout = 3x the quantum duration to allow for processing overhead
     * but not so long that we starve other threads (especially MIDI input)
     * Old 100ms timeout was causing MIDI to stop working after 1-2 minutes!
     */
    DWORD quantum_ms = (DWORD)((This->asio_current_buffersize * 1000.0) / This->asio_sample_rate);
    DWORD timeout_ms = quantum_ms * 3; /* 3x quantum duration */
    if (timeout_ms < 5) timeout_ms = 5;   /* Minimum 5ms */
    if (timeout_ms > 50) timeout_ms = 50; /* Maximum 50ms to prevent starvation */

    DWORD wait_result = WaitForSingleObject(g_callback_manager.callback_data.callback_completed, timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
        static int timeout_count = 0;
        if (timeout_count < 5) {
            WARN("ASIO callback timed out after %lu ms (quantum=%lu ms)\n", timeout_ms, quantum_ms);
            timeout_count++;
        }
        set_last_error("ASIO callback timed out");
    }
}

static void pipewire_state_changed_callback(void *data, enum pw_filter_state from, enum pw_filter_state to, char const *error) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;

    if (error) {
        WARN("state_changed: iface:%p state changed from %s to %s: %s\n", This,
             pw_filter_state_as_string(from), pw_filter_state_as_string(to), error);
    } else {
        TRACE("state_changed: iface:%p state changed from %s to %s\n", This,
              pw_filter_state_as_string(from), pw_filter_state_as_string(to));
    }
}

static void pipewire_io_changed_callback(void *data, void *port, uint32_t id, void *area, uint32_t size) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;
    (void)This; /* Used in TRACE */
    TRACE("io_changed: iface:%p IO changed on port %p: 0x%04x\n", This, port, id);
}

static void pipewire_param_changed_callback(void *data, void *port, uint32_t id, struct spa_pod const *param) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;
    (void)This; /* Used in TRACE */
    TRACE("param_changed: iface:%p param 0x%04x changed on port %p\n", This, id, port);
}

static void pipewire_add_buffer_callback(void *data, void *port, struct pw_buffer *buffer) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;

    TRACE("add_buffer: iface:%p port:%p, buffer:%p\n", This, port, buffer);

    for (int idx = 0; idx < This->wineasio_number_inputs + This->wineasio_number_outputs; ++idx) {
        IOChannel *chan = &This->input_channel[idx];
        if (chan->port == port) {
            if (chan->buffers[1]) {
                if (chan->buffers[0]) {
                    WARN("Buffers for channel %s already full\n", chan->port_name);
                    set_last_error("Buffers for channel %s already full", chan->port_name);
                    return;
                } else {
                    TRACE("Adding second buffer for channel %s\n", chan->port_name);
                    chan->buffers[0] = buffer;

                    buffer = pw_filter_dequeue_buffer(chan->port);
                    buffer->buffer->datas[0].chunk->offset = 0;
                    buffer->buffer->datas[0].chunk->stride = sizeof(float);
                    buffer->buffer->datas[0].chunk->size = 0;
                    TRACE("Dequeued buffer: %p\n", buffer);
                }
            } else {
                TRACE("Adding first buffer for channel %s\n", chan->port_name);
                chan->buffers[1] = buffer;
            }

            This->asio_buffers_left_to_init -= 1;
            if (This->asio_buffers_left_to_init == 0) {
                // Signal the creator thread
                pthread_barrier_wait(&This->asio_buffers_filled);
            }
            break;
        }
    }
}

static void pipewire_remove_buffer_callback(void *data, void *port, struct pw_buffer *buffer) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;
    (void)This; /* Used in TRACE */
    TRACE("remove_buffer: iface:%p port:%p, buffer:%p\n", This, port, buffer);
}

static void process_midi_input(IWineASIOImpl *This, uint32_t frame_count) {
    if (!This || !This->midi_input_ports || This->wineasio_number_midi_inputs <= 0) {
        return;
    }

    for (int idx = 0; idx < This->wineasio_number_midi_inputs; ++idx) {
        MidiPort *port = &This->midi_input_ports[idx];
        if (!port->active || !port->port) {
            continue;
        }

        void *buffer = pw_filter_get_dsp_buffer(port->port, frame_count);
        if (!buffer) {
            continue;
        }

        struct spa_pod *pod = (struct spa_pod *)buffer;
        if (!spa_pod_is_sequence(pod)) {
            continue;
        }

        struct spa_pod_sequence *seq = (struct spa_pod_sequence *)pod;
        struct spa_pod_control *ctrl = NULL;
        SPA_POD_SEQUENCE_FOREACH(seq, ctrl) {
            if (ctrl->type != SPA_CONTROL_Midi) {
                continue;
            }

            struct spa_pod *value = &ctrl->value;
            if (value->type != SPA_TYPE_Bytes) {
                continue;
            }

            const uint8_t *data = (const uint8_t *)SPA_POD_BODY(value);
            uint32_t size = SPA_POD_BODY_SIZE(value);
            uint32_t offset = ctrl->offset;

            if (frame_count > 0 && offset >= frame_count) {
                offset = frame_count - 1;
            }

            if (!midi_buffer_try_write(&This->midi_in_buffer, 0, offset, data, (uint8_t)size)) {
                static int drop_count = 0;
                if (drop_count < 3) {
                    WARN("MIDI input buffer full or locked, dropping event\n");
                }
                drop_count++;
            }
        }
    }
}

static void process_midi_output(IWineASIOImpl *This, uint32_t frame_count) {
    if (!This || !This->midi_output_ports || This->wineasio_number_midi_outputs <= 0) {
        return;
    }

    for (int idx = 0; idx < This->wineasio_number_midi_outputs; ++idx) {
        MidiPort *port = &This->midi_output_ports[idx];
        if (!port->active || !port->port) {
            continue;
        }

        void *buffer = pw_filter_get_dsp_buffer(port->port, frame_count);
        if (!buffer) {
            continue;
        }

        struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, This->midi_buffer_bytes);
        struct spa_pod_frame frame;
        spa_pod_builder_push_sequence(&builder, &frame, 0);

        struct midi_event ev;
        while (midi_buffer_try_read(&This->midi_out_buffer, &ev)) {
            if (ev.port != 0) {
                continue;
            }

            uint32_t offset = ev.offset;
            if (frame_count > 0 && offset >= frame_count) {
                offset = frame_count - 1;
            }

            if (spa_pod_builder_control(&builder, offset, SPA_CONTROL_Midi) < 0 ||
                spa_pod_builder_bytes(&builder, ev.data, ev.size) < 0) {
                static int build_fail_count = 0;
                if (build_fail_count < 3) {
                    WARN("MIDI output buffer overflow, dropping event\n");
                }
                build_fail_count++;
                break;
            }
        }

        spa_pod_builder_pop(&builder, &frame);
    }
}

static void pipewire_process_callback(void *data, struct spa_io_position *position) {
    IWineASIOImpl *This = (IWineASIOImpl*)data;
    int            idx;
    size_t         pw_sample_count = position->clock.duration;
    size_t         asio_sample_count = This->asio_current_buffersize;
    size_t         process_samples = asio_sample_count; /* Always use fixed ASIO buffer size */
    uint64_t       current_sample_position;
    uint64_t       current_time_stamp;

    /* DEBUG: Check for quantum mismatch */
    static int mismatch_count = 0;
    if (pw_sample_count != asio_sample_count && mismatch_count < 5) {
        printf("BUFFER SIZE MISMATCH: PipeWire quantum=%zu ASIO buffer=%zu (processing %zu samples)\n",
               pw_sample_count, asio_sample_count,
               (pw_sample_count < asio_sample_count) ? pw_sample_count : asio_sample_count);
        mismatch_count++;
        if (mismatch_count == 5) {
            printf("  -> This mismatch causes slowed/pitched audio! PipeWire quantum must match ASIO buffer size.\n");
        }
    }

    /* Fast validation - minimize branches in real-time path */
    if (unlikely(!This || This->asio_driver_state != Running || !This->asio_callbacks)) {
        /* Output silence if not ready - optimized for minimal CPU usage */
        if (This && This->output_channel) {
            for (idx = 0; idx < This->asio_active_outputs; ++idx) {
                if (This->output_channel[idx].port) {
                    void *buffer = pw_filter_get_dsp_buffer(This->output_channel[idx].port, pw_sample_count);
                    if (buffer) {
                        /* Use optimized zero-fill for silence */
                        __builtin_memset(buffer, 0, pw_sample_count * sizeof(float));
                    }
                }
            }
        }
        return;
    }

    /* Handle variable PipeWire buffer sizes - optimized path */
    if (unlikely(pw_sample_count != asio_sample_count)) {
        /* Use the smaller of the two to prevent buffer overruns */
        process_samples = (pw_sample_count < asio_sample_count) ? pw_sample_count : asio_sample_count;
    }

    /* Pre-calculate common values to avoid repeated calculations */
    size_t asio_buffer_bytes;
    size_t process_bytes;
    LONG input_buffer_index;
    LONG current_buffer_index;
    
    asio_buffer_bytes = asio_sample_count * sizeof(float);
    process_bytes = process_samples * sizeof(float);
    input_buffer_index = This->asio_buffer_index;
    
    /* Optimized input processing - minimize memory operations */
    for (idx = 0; idx < This->asio_active_inputs; ++idx) {
        IOChannel *chan = &This->input_channel[idx];
        if (likely(chan->active && chan->port && chan->wine_buffers[input_buffer_index])) {
            void *pw_buffer = pw_filter_get_dsp_buffer(chan->port, pw_sample_count);
            
            if (likely(pw_buffer && chan->buffer_size >= asio_buffer_bytes)) {
                if (likely(process_samples == asio_sample_count)) {
                    /* Fast path: direct copy with compiler optimization hints */
                    __builtin_memcpy(chan->wine_buffers[input_buffer_index], pw_buffer, asio_buffer_bytes);
                } else {
                    /* Slower path: copy + zero-pad */
                    __builtin_memcpy(chan->wine_buffers[input_buffer_index], pw_buffer, process_bytes);
                    
                    /* Zero-pad remaining space efficiently */
                    if (process_bytes < asio_buffer_bytes) {
                        __builtin_memset((char*)chan->wine_buffers[input_buffer_index] + process_bytes, 0, 
                                       asio_buffer_bytes - process_bytes);
                    }
                }
            }
        }
    }

    /* Capture MIDI input events before invoking the ASIO callback */
    process_midi_input(This, (uint32_t)process_samples);

    /* Capture current timing before calling the ASIO callback */
    current_sample_position = This->asio_sample_position;

    if (likely(!(position->clock.flags & SPA_IO_CLOCK_FLAG_FREEWHEEL))) {
        /* Use PipeWire's clock time (nanoseconds) */
        current_time_stamp = position->clock.nsec;
    } else {
        /* Freewheel mode - calculate from sample position (nanoseconds) */
        current_time_stamp = (uint64_t)((double)current_sample_position * 1000000000.0 / This->asio_sample_rate);
    }

    /* Expose current timing for ASIO time info and GetSamplePosition */
    This->asio_time_stamp = current_time_stamp;

    /* Marshal ASIO callback - capture buffer index before it changes */
    current_buffer_index = This->asio_buffer_index;

    /* Optimized callback marshalling */
    if (likely(This->asio_time_info_mode)) {
        /* Pre-fill time structure for efficiency */
        This->asio_time.timeInfo.samplePosition = ASIO_LONG(ASIOSamples, current_sample_position);
        This->asio_time.timeInfo.systemTime = ASIO_LONG(ASIOTimeStamp, current_time_stamp);
        This->asio_time.timeInfo.sampleRate = This->asio_sample_rate;
        This->asio_time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        marshal_asio_callback(This, current_buffer_index, ASIOTrue, &This->asio_time, true);
    } else {
        marshal_asio_callback(This, current_buffer_index, ASIOTrue, NULL, false);
    }

    /* Advance timing after the callback */
    This->asio_sample_position = current_sample_position + asio_sample_count;
    if (This->asio_sample_rate > 0) {
        uint64_t buffer_duration_ns = (asio_sample_count * 1000000000ULL) / (uint64_t)This->asio_sample_rate;
        This->asio_time_stamp = current_time_stamp + buffer_duration_ns;
    }

    /* Optimized output processing - minimize memory operations */
    for (idx = 0; idx < This->asio_active_outputs; ++idx) {
        IOChannel *chan = &This->output_channel[idx];
        if (likely(chan->active && chan->port && chan->wine_buffers[current_buffer_index])) {
            void *pw_buffer = pw_filter_get_dsp_buffer(chan->port, pw_sample_count);
            
            if (likely(pw_buffer && chan->buffer_size >= asio_buffer_bytes)) {
                if (likely(pw_sample_count == asio_sample_count)) {
                    /* Fast path: direct copy */
                    __builtin_memcpy(pw_buffer, chan->wine_buffers[current_buffer_index], asio_buffer_bytes);
                } else {
                    /* Handle size mismatch efficiently */
                    const size_t pw_buffer_bytes = pw_sample_count * sizeof(float);
                    const size_t copy_bytes = (pw_sample_count < asio_sample_count) ? 
                                            pw_buffer_bytes : asio_buffer_bytes;
                    
                    __builtin_memcpy(pw_buffer, chan->wine_buffers[current_buffer_index], copy_bytes);
                    
                    /* Zero-pad remaining PipeWire buffer space if needed */
                    if (copy_bytes < pw_buffer_bytes) {
                        __builtin_memset((char*)pw_buffer + copy_bytes, 0, pw_buffer_bytes - copy_bytes);
                    }
                }
            }
        }
    }

    /* Flush MIDI output events after the ASIO callback */
    process_midi_output(This, (uint32_t)process_samples);

    /* Atomic buffer index switch for thread safety */
    This->asio_buffer_index ^= 1;
}

/* Worker callback function to handle deferred PipeWire operations */
static int pipewire_worker_callback(void *userdata, enum pw_op_type operation) {
    IWineASIOImpl *This = (IWineASIOImpl*)userdata;
    int result = 0;
    
    TRACE("Worker callback: operation=%d, driver=%p\n", operation, This);
    
    switch (operation) {
        case PW_OP_CONNECT_FILTER: {
            /* This executes in the correct PipeWire thread context */
            char pod_buffer[0x1000];
            struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof pod_buffer);
            struct spa_audio_info_raw format = SPA_AUDIO_INFO_RAW_INIT(
                .format = SPA_AUDIO_FORMAT_F32,
                .rate = This->asio_sample_rate,
                .channels = This->asio_active_outputs,
            );
            
            /* Calculate latency in nanoseconds from buffer size and sample rate */
            uint64_t latency_ns = (uint64_t)This->asio_current_buffersize * SPA_NSEC_PER_SEC / (uint64_t)This->asio_sample_rate;
            
            struct spa_pod const *connect_params[] = {
                spa_format_audio_raw_build(&pod_builder, SPA_PARAM_EnumFormat, &format),
                spa_process_latency_build(&pod_builder, SPA_PARAM_ProcessLatency,
                    &SPA_PROCESS_LATENCY_INFO_INIT(.ns = latency_ns)),
            };

            /* Set quantum to match ASIO buffer size */
            char rate_str[32], latency_str[32];
            uint32_t quantum = This->asio_current_buffersize;
            uint32_t rate = (uint32_t)This->asio_sample_rate;
            snprintf(rate_str, sizeof(rate_str), "%u/%u", quantum, rate);
            snprintf(latency_str, sizeof(latency_str), "%u/%u", quantum, rate);

            const struct spa_dict_item items[] = {
                { PW_KEY_NODE_RATE, rate_str },
                { PW_KEY_NODE_LATENCY, latency_str },
            };
            const struct spa_dict dict = SPA_DICT_INIT(items, 2);
            pw_filter_update_properties(This->pw_filter, NULL, &dict);

            TRACE("Worker: Connecting PipeWire filter in correct thread context\n");
            TRACE("Worker: Connecting PipeWire filter with %d samples (%.2f ms) at %.0f Hz\n",
                  This->asio_current_buffersize,
                  (double)This->asio_current_buffersize * 1000.0 / This->asio_sample_rate,
                  This->asio_sample_rate);

    if (pw_filter_connect(This->pw_filter, PW_FILTER_FLAG_RT_PROCESS, connect_params, ARRAYSIZE(connect_params)) < 0) {
        ERR("Worker: Failed to connect PipeWire filter\n");
        set_last_error("Worker failed to connect PipeWire filter");
        result = -1;
    } else {
        TRACE("Worker: PipeWire filter connected successfully\n");
    }

            break;
        }
        default:
            WARN("Worker: Unknown operation type: %d\n", operation);
            set_last_error("Worker received unknown operation");
            result = -1;
            break;
    }
    
    return result;
}

static struct pw_filter_events const pw_filter_events = {
    .version = PW_VERSION_FILTER_EVENTS,
    .state_changed = pipewire_state_changed_callback,
    .io_changed = pipewire_io_changed_callback,
    .param_changed = pipewire_param_changed_callback,
    .add_buffer = pipewire_add_buffer_callback,
    .remove_buffer = pipewire_remove_buffer_callback,
    .process = pipewire_process_callback,
};

/*****************************************************************************
 * WineASIO instance creation
 */

HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj, IUnknown *cls_factory)
{
    IWineASIOImpl   *pobj;

    /* TRACE("riid: %s, ppobj: %p\n", debugstr_guid(riid), ppobj); */

    pobj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pobj));
    if (pobj == NULL)
    {
        WARN("out of memory\n");
        return E_OUTOFMEMORY;
    }

    pobj->lpVtbl = &WineASIO_Vtbl;
    pobj->ref = 1;
    pobj->cls_factory = cls_factory;
    cls_factory->lpVtbl->AddRef(cls_factory);
    pobj->asio_driver_state = Loaded;
    pobj->asio_sample_rate = PW_ASIO_DEFAULT_SAMPLE_RATE;
    pobj->asio_current_buffersize = ASIO_PREFERRED_BUFFERSIZE;
    pobj->wineasio_preferred_buffersize = ASIO_PREFERRED_BUFFERSIZE;
    pobj->wineasio_number_inputs = PW_ASIO_DEFAULT_INPUT_CHANNELS;
    pobj->wineasio_number_outputs = PW_ASIO_DEFAULT_OUTPUT_CHANNELS;
    pobj->wineasio_number_midi_inputs = PW_ASIO_DEFAULT_MIDI_INPUTS;
    pobj->wineasio_number_midi_outputs = PW_ASIO_DEFAULT_MIDI_OUTPUTS;
    pobj->wineasio_connect_to_hardware = true;
    pobj->wineasio_fixed_buffersize = false;
    pobj->midi_input_ports = NULL;
    pobj->midi_output_ports = NULL;
    pobj->midi_buffers_initialized = false;
    pobj->midi_buffer_bytes = PW_ASIO_MIDI_BUFFER_BYTES;

    midi_buffer_init(&pobj->midi_in_buffer);
    midi_buffer_init(&pobj->midi_out_buffer);
    pobj->midi_buffers_initialized = true;
    pobj->asio_version = 1;
    pobj->node_resolve_thread = NULL;
    pobj->node_resolve_stop = 0;
    TRACE("pobj = %p\n", pobj);
    *ppobj = pobj;
    /* TRACE("return %p\n", *ppobj); */
    return S_OK;
}

/*****************************************************************************
 * Interface method definitions
 */


HIDDEN HRESULT STDMETHODCALLTYPE QueryInterface(LPWINEASIO iface, REFIID riid, void **ppvObject)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;

    TRACE("iface: %p, riid: %s, ppvObject: %p)\n", iface, debugstr_guid(riid), ppvObject);

    if (ppvObject == NULL)
        return E_INVALIDARG;

    if (IsEqualIID(&CLSID_PipeWine, riid))
    {
        AddRef(iface);
        *ppvObject = This;
        return S_OK;
    }

    return E_NOINTERFACE;
}

/*
 * ULONG STDMETHODCALLTYPE AddRef(LPWINEASIO iface);
 * Function: Increment the reference count on the object
 * Returns:  Ref count
 */

HIDDEN ULONG STDMETHODCALLTYPE AddRef(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    ULONG           ref = InterlockedIncrement(&(This->ref));

    TRACE("iface: %p, ref count is %d\n", iface, ref);
    return ref;
}

/*
 * ULONG Release (LPWINEASIO iface);
 *  Function:   Destroy the interface
 *  Returns:    Ref count
 *  Implies:    ASIOStop() and ASIODisposeBuffers()
 */

HIDDEN ULONG STDMETHODCALLTYPE Release(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;
    ULONG            ref = InterlockedDecrement(&This->ref);

    TRACE("iface: %p, ref count is %d\n", iface, ref);

    if (This->asio_driver_state == Running)
        Stop(iface);
    if (This->asio_driver_state == Prepared)
        DisposeBuffers(iface);

    if (This->asio_driver_state == Initialized)
    {
        /* just for good measure we deinitialize IOChannel structures */
        for (int i = 0; i < This->wineasio_number_inputs; i++)
        {
            This->input_channel[i].active = false;
        }
        for (int i = 0; i < This->wineasio_number_outputs; i++)
        {
            This->output_channel[i].active = false;
        }
        This->asio_active_inputs = This->asio_active_outputs = 0;
        TRACE("%i IOChannel structures released\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

        if (This->input_channel)
            HeapFree(GetProcessHeap(), 0, This->input_channel);
        This->input_channel = NULL;
        This->output_channel = NULL;

        if (This->midi_input_ports) {
            HeapFree(GetProcessHeap(), 0, This->midi_input_ports);
            This->midi_input_ports = NULL;
            This->midi_output_ports = NULL;
        }

        /* Cleanup PipeWire resources */
        Uninit(This);
    }
    if (ref == 0) {
        /* Cleanup ASIO callback manager on final release */
        cleanup_asio_callback_manager();

        if (This->midi_buffers_initialized) {
            midi_buffer_destroy(&This->midi_in_buffer);
            midi_buffer_destroy(&This->midi_out_buffer);
            This->midi_buffers_initialized = false;
        }
        
        TRACE("PipeWine terminated\n\n");
        This->cls_factory->lpVtbl->Release(This->cls_factory);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void Uninit(IWineASIOImpl *This) {
    if (!This) return;

    if (This->node_resolve_thread) {
        InterlockedExchange(&This->node_resolve_stop, 1);
        WaitForSingleObject(This->node_resolve_thread, 2000);
        CloseHandle(This->node_resolve_thread);
        This->node_resolve_thread = NULL;
    }

    /* Cleanup PipeWire filter if exists */
    if (This->pw_filter) {
        user_pw_lock_loop(This->pw_helper);
        pw_filter_disconnect(This->pw_filter);
        user_pw_unlock_loop(This->pw_helper);
        pw_filter_destroy(This->pw_filter);
        This->pw_filter = NULL;
    }

    /* Cleanup PipeWire helper */
    if (This->pw_helper) {
        user_pw_destroy_helper(This->pw_helper);
        This->pw_helper = NULL;
    }

    /* Reset state */
    This->pw_loop = NULL;
    This->pw_context = NULL;
    This->pw_core = NULL;
    This->current_input_node = NULL;
    This->current_output_node = NULL;
    This->node_resolve_stop = 0;
}

/* Helper function to clear all audio buffers for clean state transitions */
static void clear_audio_buffers(IWineASIOImpl *This, const char *context) {
    int cleared_buffers = 0;
    
    TRACE("Clearing audio buffers (%s)\n", context);
    
    /* Clear input channel buffers */
    for (int i = 0; i < This->wineasio_number_inputs; i++) {
        if (This->input_channel[i].active) {
            /* Clear Wine-allocated buffers with bounds checking */
            if (This->input_channel[i].wine_buffers[0] && This->input_channel[i].buffer_size > 0) {
                memset(This->input_channel[i].wine_buffers[0], 0, This->input_channel[i].buffer_size);
                cleared_buffers++;
            }
            if (This->input_channel[i].wine_buffers[1] && This->input_channel[i].buffer_size > 0) {
                memset(This->input_channel[i].wine_buffers[1], 0, This->input_channel[i].buffer_size);
                cleared_buffers++;
            }
        }
    }
    
    /* Clear output channel buffers */
    for (int i = 0; i < This->wineasio_number_outputs; i++) {
        if (This->output_channel[i].active) {
            /* Clear Wine-allocated buffers with bounds checking */
            if (This->output_channel[i].wine_buffers[0] && This->output_channel[i].buffer_size > 0) {
                memset(This->output_channel[i].wine_buffers[0], 0, This->output_channel[i].buffer_size);
                cleared_buffers++;
            }
            if (This->output_channel[i].wine_buffers[1] && This->output_channel[i].buffer_size > 0) {
                memset(This->output_channel[i].wine_buffers[1], 0, This->output_channel[i].buffer_size);
                cleared_buffers++;
            }
        }
    }
    
    TRACE("Cleared %d audio buffers (%s)\n", cleared_buffers, context);
}

static ASIOError InitPorts(IWineASIOImpl *This) {
    int idx;
    char pod_buffer[0x1000];
    struct spa_pod_builder pod_builder;

    printf("PipeWine ASIO: InitPorts() starting\n");
    /* Allocate IOChannel structures */
    This->input_channel = HeapAlloc(GetProcessHeap(), 0, (This->wineasio_number_inputs + This->wineasio_number_outputs) * sizeof(IOChannel));
    if (!This->input_channel)
    {
        ERR("Unable to allocate IOChannel structures for %i channels\n", This->wineasio_number_inputs + This->wineasio_number_outputs);
        set_last_error("Unable to allocate IOChannel structures");
        return ASE_NoMemory;
    }
    This->output_channel = This->input_channel + This->wineasio_number_inputs;
    TRACE("%i IOChannel structures allocated\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

    /* Set up ports */
    pod_builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof pod_buffer);

    struct spa_pod const *port_params[] = {
        spa_pod_builder_add_object(&pod_builder,
            SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(2),
            SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(SPA_DATA_MemPtr),
            SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_STEP_Int(
                This->asio_current_buffersize * sizeof(float),
                This->asio_current_buffersize * sizeof(float),
                This->asio_current_buffersize * sizeof(float),
                sizeof(float)
            ),
            SPA_PARAM_BUFFERS_stride, SPA_POD_Int(sizeof(float))
        ),
        spa_pod_builder_add_object(&pod_builder,
            SPA_TYPE_OBJECT_ParamIO, SPA_PARAM_IO,
            SPA_PARAM_IO_id, SPA_POD_Id(SPA_IO_Buffers),
            SPA_PARAM_IO_size, SPA_POD_Int(sizeof(struct spa_io_buffers))
        ),
    };

    #define INPUT_PORT_PREFIX "input_"
    for (idx = 0; idx < This->wineasio_number_inputs; ++idx) {
        snprintf(This->input_channel[idx].port_name, ASIO_MAX_NAME_LENGTH, INPUT_PORT_PREFIX "%d", idx);
        
        /* Initialize Wine buffer management fields */
        This->input_channel[idx].wine_buffers[0] = NULL;
        This->input_channel[idx].wine_buffers[1] = NULL;
        This->input_channel[idx].buffer_size = 0;
        This->input_channel[idx].needs_copy = true;
        
        This->input_channel[idx].port = pw_filter_add_port(This->pw_filter,
            PW_DIRECTION_INPUT,
            PW_FILTER_PORT_FLAG_MAP_BUFFERS,
            0,
            pw_properties_new(
                PW_KEY_PORT_NAME, This->input_channel[idx].port_name,
                PW_KEY_FORMAT_DSP, JACK_DEFAULT_AUDIO_TYPE,
                NULL),
            port_params, ARRAYSIZE(port_params));
    }
    #define OUTPUT_PORT_PREFIX "output_"
    for (idx = 0; idx < This->wineasio_number_outputs; ++idx) {
        snprintf(This->output_channel[idx].port_name, ASIO_MAX_NAME_LENGTH, OUTPUT_PORT_PREFIX "%d", idx);
        
        /* Initialize Wine buffer management fields */
        This->output_channel[idx].wine_buffers[0] = NULL;
        This->output_channel[idx].wine_buffers[1] = NULL;
        This->output_channel[idx].buffer_size = 0;
        This->output_channel[idx].needs_copy = true;
        
        This->output_channel[idx].port = pw_filter_add_port(This->pw_filter,
            PW_DIRECTION_OUTPUT,
            PW_FILTER_PORT_FLAG_MAP_BUFFERS,
            0,
            pw_properties_new(
                PW_KEY_PORT_NAME, This->output_channel[idx].port_name,
                PW_KEY_FORMAT_DSP, JACK_DEFAULT_AUDIO_TYPE,
                NULL),
            port_params, ARRAYSIZE(port_params));
    }
    TRACE("%i IOChannel structures initialized\n", This->wineasio_number_inputs + This->wineasio_number_outputs);

    if (This->wineasio_number_midi_inputs > 0 || This->wineasio_number_midi_outputs > 0) {
        int midi_total = This->wineasio_number_midi_inputs + This->wineasio_number_midi_outputs;
        This->midi_input_ports = HeapAlloc(GetProcessHeap(), 0, midi_total * sizeof(MidiPort));
        if (!This->midi_input_ports) {
            ERR("Unable to allocate MidiPort structures for %i ports\n", midi_total);
            set_last_error("Unable to allocate MidiPort structures");
            return ASE_NoMemory;
        }
        This->midi_output_ports = This->midi_input_ports + This->wineasio_number_midi_inputs;

        char midi_pod_buffer[0x1000];
        struct spa_pod_builder midi_builder = SPA_POD_BUILDER_INIT(midi_pod_buffer, sizeof midi_pod_buffer);

        struct spa_pod const *midi_port_params[] = {
            spa_pod_builder_add_object(&midi_builder,
                SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(2),
                SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
                SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(SPA_DATA_MemPtr),
                SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_STEP_Int(
                    (int)This->midi_buffer_bytes,
                    (int)This->midi_buffer_bytes,
                    (int)This->midi_buffer_bytes,
                    16
                ),
                SPA_PARAM_BUFFERS_stride, SPA_POD_Int(1)
            ),
            spa_pod_builder_add_object(&midi_builder,
                SPA_TYPE_OBJECT_ParamIO, SPA_PARAM_IO,
                SPA_PARAM_IO_id, SPA_POD_Id(SPA_IO_Buffers),
                SPA_PARAM_IO_size, SPA_POD_Int(sizeof(struct spa_io_buffers))
            ),
            spa_pod_builder_add_object(&midi_builder,
                SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_application),
                SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_control),
                SPA_FORMAT_CONTROL_types, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_CONTROL_Midi)
            ),
        };

        #define MIDI_INPUT_PORT_PREFIX "midi_in_"
        for (idx = 0; idx < This->wineasio_number_midi_inputs; ++idx) {
            snprintf(This->midi_input_ports[idx].port_name, ASIO_MAX_NAME_LENGTH, MIDI_INPUT_PORT_PREFIX "%d", idx);
            This->midi_input_ports[idx].active = true;
            This->midi_input_ports[idx].port = pw_filter_add_port(This->pw_filter,
                PW_DIRECTION_INPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                0,
                pw_properties_new(
                    PW_KEY_PORT_NAME, This->midi_input_ports[idx].port_name,
                    PW_KEY_FORMAT_DSP, JACK_DEFAULT_MIDI_TYPE,
                    NULL),
                midi_port_params, ARRAYSIZE(midi_port_params));
        }
        #define MIDI_OUTPUT_PORT_PREFIX "midi_out_"
        for (idx = 0; idx < This->wineasio_number_midi_outputs; ++idx) {
            snprintf(This->midi_output_ports[idx].port_name, ASIO_MAX_NAME_LENGTH, MIDI_OUTPUT_PORT_PREFIX "%d", idx);
            This->midi_output_ports[idx].active = true;
            This->midi_output_ports[idx].port = pw_filter_add_port(This->pw_filter,
                PW_DIRECTION_OUTPUT,
                PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                0,
                pw_properties_new(
                    PW_KEY_PORT_NAME, This->midi_output_ports[idx].port_name,
                    PW_KEY_FORMAT_DSP, JACK_DEFAULT_MIDI_TYPE,
                    NULL),
                midi_port_params, ARRAYSIZE(midi_port_params));
        }
    }

    printf("PipeWine ASIO: InitPorts() completed\n");
    return ASE_OK;
}

/*
 * ASIOBool Init (void *sysRef);
 *  Function:   Initialize the driver
 *  Parameters: Pointer to "This"
 *              sysHanle is 0 on OS/X and on windows it contains the applications main window handle
 *  Returns:    ASIOFalse on error, and ASIOTrue on success
 */

DEFINE_THISCALL_WRAPPER(Init,8)
HIDDEN ASIOBool STDMETHODCALLTYPE Init(LPWINEASIO iface, void *sysRef)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;

    struct pw_helper_init_args init_args = {
        .app_name = This->client_name,
        .loop = &This->pw_loop,
        .context = &This->pw_context,
        .core = &This->pw_core,
        .thread_creator = jack_thread_creator,
    };

    This->sys_ref = sysRef;
    configure_driver(This);

    if (!(This->pw_helper = user_pw_create_helper(0, NULL, &init_args)))
    {
        set_last_error("Failed to initialize PipeWire helper");
        return ASIOFalse;
    }

    printf("PipeWine ASIO: Helper created, registering worker callback\n");
    /* Register the worker callback for deferred PipeWire operations */
    user_pw_set_worker_callback(pipewire_worker_callback);

    printf("PipeWine ASIO: Setting up GUI config\n");
    This->gui = NULL;
    This->gui_conf.user = This;
    This->gui_conf.closed = GuiClosed;
    This->gui_conf.apply_config = GuiApplyConfig;
    This->gui_conf.load_config = GuiLoadConfig;
    This->gui_conf.pw_helper = This->pw_helper;
    This->gui_conf.cf_buffer_size = 1024;

    printf("PipeWine ASIO: Getting nodes by name\n");
    get_nodes_by_name(This);
    ensure_nodes_ready_async(This);

    if (This->current_input_node)
        TRACE("Selected input node: %u\n", pw_proxy_get_bound_id((struct pw_proxy *)This->current_input_node));
    if (This->current_output_node)
        TRACE("Selected output node: %u\n", pw_proxy_get_bound_id((struct pw_proxy *)This->current_output_node));

    printf("PipeWine ASIO: Locking PipeWire loop\n");
    user_pw_lock_loop(This->pw_helper);

    This->pw_filter = pw_filter_new(This->pw_core, This->client_name, pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_MEDIA_CLASS, "Stream/Audio",
        PW_KEY_NODE_AUTOCONNECT, "true",
        NULL
    ));

    if (!This->pw_filter) {
        user_pw_unlock_loop(This->pw_helper);
        ERR("Failed to create filter node\n");
        set_last_error("Failed to create PipeWire filter node");
        return ASIOFalse;
    }

    pw_filter_add_listener(This->pw_filter, &This->pw_filter_listener, &pw_filter_events, This);

    if (InitPorts(This) != ASE_OK) {
        user_pw_unlock_loop(This->pw_helper);
        set_last_error("Failed to initialize PipeWire ports");
        return ASIOFalse;
    }

    user_pw_unlock_loop(This->pw_helper);

    This->asio_driver_state = Initialized;
    printf("PipeWine ASIO: Init() completed successfully!\n");
    TRACE("PipeWine 0.%d.%d initialized\n", This->asio_version / 10, This->asio_version % 10);
    return ASIOTrue;
}

/*
 * void GetDriverName(char *name);
 *  Function:    Returns the driver name in name
 */

DEFINE_THISCALL_WRAPPER(GetDriverName,8)
HIDDEN void STDMETHODCALLTYPE GetDriverName(LPWINEASIO iface, char *name)
{
    TRACE("iface: %p, name: %p\n", iface, name);
    strcpy(name, "PipeWine");
    return;
}

/*
 * LONG GetDriverVersion (void);
 *  Function:    Returns the driver version number
 */

DEFINE_THISCALL_WRAPPER(GetDriverVersion,4)
HIDDEN LONG STDMETHODCALLTYPE GetDriverVersion(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);
    return This->asio_version;
}

/*
 * void GetErrorMessage(char *string);
 *  Function:    Returns an error message for the last occured error in string
 */

DEFINE_THISCALL_WRAPPER(GetErrorMessage,8)
HIDDEN void STDMETHODCALLTYPE GetErrorMessage(LPWINEASIO iface, char *string)
{
    TRACE("iface: %p, string: %p)\n", iface, string);
    if (string) {
        strncpy(string, g_last_error, 124);
        string[123] = '\0';
    }
}

/*
 * ASIOError Start(void);
 *  Function:    Start PipeWire IO processing and reset the sample counter to zero
 *  Returns:     ASE_NotPresent if IO is missing
 *               ASE_HWMalfunction if PipeWire fails to start
 */

DEFINE_THISCALL_WRAPPER(Start,4)
HIDDEN ASIOError STDMETHODCALLTYPE Start(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state != Prepared) {
        set_last_error("Driver is not prepared");
        return ASE_NotPresent;
    }

    /* Clear and resync all buffers to prevent distorted audio on restart */
    clear_audio_buffers(This, "driver start");
    if (This->midi_buffers_initialized) {
        midi_buffer_clear(&This->midi_in_buffer);
        midi_buffer_clear(&This->midi_out_buffer);
    }

    /* Activate the PipeWire filter for streaming */
    user_pw_lock_loop(This->pw_helper);
    pw_filter_set_active(This->pw_filter, true);
    user_pw_unlock_loop(This->pw_helper);

    /* Wait for filter to reach streaming state - use longer timeout for small buffer sizes */
    int streaming_timeout = (This->asio_current_buffersize <= 128) ? 8000 : 5000;
    TRACE("Waiting for PipeWire filter to reach streaming state (timeout: %d ms)...\n", streaming_timeout);
    if (!user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_STREAMING, streaming_timeout)) {
        WARN("Filter did not reach streaming state within timeout, continuing anyway\n");
        set_last_error("Filter did not reach streaming state within timeout");
        /* Don't fail - some configurations may work without reaching streaming immediately */
    } else {
        TRACE("PipeWire filter successfully reached streaming state\n");
    }

    /* Initialize ASIO timing and buffer state - ensure clean restart */
    This->asio_buffer_index = 0;
    This->asio_sample_position = 0;
    /* Initialize timestamp in nanoseconds */
    This->asio_time_stamp = timeGetTime() * 1000000ULL; /* Convert milliseconds to nanoseconds */

    /* Prepare ASIO timing structures */
    if (This->asio_time_info_mode) {
        This->asio_time.timeInfo.samplePosition = ASIO_LONG(ASIOSamples, 0);
        This->asio_time.timeInfo.systemTime = ASIO_LONG(ASIOTimeStamp, This->asio_time_stamp);
        This->asio_time.timeInfo.sampleRate = This->asio_sample_rate;
        This->asio_time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;

        if (This->asio_can_time_code) {
            This->asio_time.timeCode.speed = 1;
            This->asio_time.timeCode.timeCodeSamples = ASIO_LONG(ASIOSamples, This->asio_sample_position);
            This->asio_time.timeCode.flags = ~(kTcValid | kTcRunning);
        }
    }

    /* Set driver state to Running - this enables the process callback */
    This->asio_driver_state = Running;
    TRACE("PipeWine successfully started with clean buffers\n");
    return ASE_OK;
}

/*
 * ASIOError Stop(void);
 *  Function:   Stop PipeWire IO processing
 *  Returns:    ASE_NotPresent on missing IO
 *  Note:       BufferSwitch() must not called after returning
 */

DEFINE_THISCALL_WRAPPER(Stop,4)
HIDDEN ASIOError STDMETHODCALLTYPE Stop(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state != Running) {
        set_last_error("Driver is not running");
        return ASE_NotPresent;
    }

    /* Deactivate the PipeWire filter first to stop audio processing */
    user_pw_lock_loop(This->pw_helper);
    pw_filter_set_active(This->pw_filter, false);
    user_pw_unlock_loop(This->pw_helper);

    /* Clear buffers to ensure clean state for next start */
    clear_audio_buffers(This, "driver stop");
    if (This->midi_buffers_initialized) {
        midi_buffer_clear(&This->midi_in_buffer);
        midi_buffer_clear(&This->midi_out_buffer);
    }

    /* Reset buffer index to ensure consistent state */
    This->asio_buffer_index = 0;

    This->asio_driver_state = Prepared;

    TRACE("PipeWine stopped with clean buffer state\n");
    return ASE_OK;
}

/*
 * ASIOError GetChannels(LONG *numInputChannels, LONG *numOutputChannels);
 *  Function:   Report number of IO channels
 *  Parameters: numInputChannels and numOutputChannels will hold number of channels on returning
 *  Returns:    ASE_NotPresent if no channels are available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetChannels,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetChannels (LPWINEASIO iface, LONG *numInputChannels, LONG *numOutputChannels)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    if (!numInputChannels || !numOutputChannels)
        return ASE_InvalidParameter;

    *numInputChannels = This->wineasio_number_inputs;
    *numOutputChannels = This->wineasio_number_outputs;
    TRACE("iface: %p, inputs: %i, outputs: %i\n", iface, This->wineasio_number_inputs, This->wineasio_number_outputs);
    return ASE_OK;
}

/*
 * ASIOError GetLatencies(LONG *inputLatency, LONG *outputLatency);
 *  Function:   Return latency in frames
 *  Returns:    ASE_NotPresent if no IO is available, otherwise AES_OK
 */

DEFINE_THISCALL_WRAPPER(GetLatencies,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetLatencies(LPWINEASIO iface, LONG *inputLatency, LONG *outputLatency)
{
    IWineASIOImpl           *This = (IWineASIOImpl*)iface;

    if (!inputLatency || !outputLatency)
        return ASE_InvalidParameter;

    if (This->asio_driver_state == Loaded)
        return ASE_NotPresent;

    TRACE("iface: %p, input latency: %d, output latency: %d\n", iface, *inputLatency, *outputLatency);

    *inputLatency = This->asio_current_buffersize;
    *outputLatency = This->asio_current_buffersize;
    return ASE_OK;
}

/*
 * ASIOError GetBufferSize(LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity);
 *  Function:    Return minimum, maximum, preferred buffer sizes, and granularity
 *               At the moment return all the same, and granularity 0
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetBufferSize,20)
HIDDEN ASIOError STDMETHODCALLTYPE GetBufferSize(LPWINEASIO iface, LONG *minSize, LONG *maxSize, LONG *preferredSize, LONG *granularity)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, minSize: %p, maxSize: %p, preferredSize: %p, granularity: %p\n", iface, minSize, maxSize, preferredSize, granularity);

    if (!minSize || !maxSize || !preferredSize || !granularity)
        return ASE_InvalidParameter;

    if (This->wineasio_fixed_buffersize)
    {
        *minSize = *maxSize = *preferredSize = This->asio_current_buffersize;
        *granularity = 0;
        TRACE("Buffersize fixed at %i\n", This->asio_current_buffersize);
        return ASE_OK;
    }

    *minSize = ASIO_MINIMUM_BUFFERSIZE;
    *maxSize = ASIO_MAXIMUM_BUFFERSIZE;
    *preferredSize = This->wineasio_preferred_buffersize;
    *granularity = 1;
    TRACE("The ASIO host can control buffersize\nMinimum: %i, maximum: %i, preferred: %i, granularity: %i, current: %i\n",
          *minSize, *maxSize, *preferredSize, *granularity, This->asio_current_buffersize);
    return ASE_OK;
}

/*
 * ASIOError CanSampleRate(ASIOSampleRate sampleRate);
 *  Function:   Ask if specific SR is available
 *  Returns:    ASE_NoClock if SR isn't available, ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(CanSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE CanSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    (void)This;

    TRACE("iface: %p, Samplerate = %li, requested samplerate = %li\n", iface, (long) This->asio_sample_rate, (long) sampleRate);

    return ASE_OK;
}

/*
 * ASIOError GetSampleRate(ASIOSampleRate *currentRate);
 *  Function:   Return current SR
 *  Parameters: currentRate will hold SR on return, 0 if unknown
 *  Returns:    ASE_NoClock if SR is unknown, ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetSampleRate,8)
HIDDEN ASIOError STDMETHODCALLTYPE GetSampleRate(LPWINEASIO iface, ASIOSampleRate *sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate is %i\n", iface, (int) This->asio_sample_rate);

    if (!sampleRate)
        return ASE_InvalidParameter;

    *sampleRate = This->asio_sample_rate;
    return ASE_OK;
}

/*
 * ASIOError SetSampleRate(ASIOSampleRate sampleRate);
 *  Function:   Set requested SR, enable external sync if SR == 0
 *  Returns:    ASE_NoClock if unknown SR
 *              ASE_InvalidMode if current clock is external and SR != 0
 *              ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(SetSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE SetSampleRate(LPWINEASIO iface, ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, Sample rate %f requested\n", iface, sampleRate);

    This->asio_sample_rate = sampleRate;
    return ASE_OK;
}

/*
 * ASIOError GetClockSources(ASIOClockSource *clocks, LONG *numSources);
 *  Function:   Return available clock sources
 *  Parameters: clocks - a pointer to an array of ASIOClockSource structures.
 *              numSources - when called: number of allocated members
 *                         - on return: number of clock sources, the minimum is 1 - the internal clock
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetClockSources,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetClockSources(LPWINEASIO iface, ASIOClockSource *clocks, LONG *numSources)
{
    TRACE("iface: %p, clocks: %p, numSources: %p\n", iface, clocks, numSources);

    if (!clocks || !numSources)
        return ASE_InvalidParameter;

    clocks->index = 0;
    clocks->associatedChannel = -1;
    clocks->associatedGroup = -1;
    clocks->isCurrentSource = ASIOTrue;
    strcpy(clocks->name, "Internal");
    *numSources = 1;
    return ASE_OK;
}

/*
 * ASIOError SetClockSource(LONG index);
 *  Function:   Set clock source
 *  Parameters: index returned by ASIOGetClockSources() - See asio.h for more details
 *  Returns:    ASE_NotPresent on missing IO
 *              ASE_InvalidMode may be returned if a clock can't be selected
 *              ASE_NoClock should not be returned
 */

DEFINE_THISCALL_WRAPPER(SetClockSource,8)
HIDDEN ASIOError STDMETHODCALLTYPE SetClockSource(LPWINEASIO iface, LONG index)
{
    TRACE("iface: %p, index: %i\n", iface, index);

    if (index != 0)
        return ASE_NotPresent;
    return ASE_OK;
}

/*
 * ASIOError GetSamplePosition (ASIOSamples *sPos, ASIOTimeStamp *tStamp);
 *  Function:   Return sample position and timestamp
 *  Parameters: sPos holds the position on return, reset to 0 on ASIOStart()
 *              tStamp holds the system time of sPos
 *  Return:     ASE_NotPresent on missing IO
 *              ASE_SPNotAdvancing on missing clock
 */

DEFINE_THISCALL_WRAPPER(GetSamplePosition,12)
HIDDEN ASIOError STDMETHODCALLTYPE GetSamplePosition(LPWINEASIO iface, ASIOSamples *sPos, ASIOTimeStamp *tStamp)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p, sPos: %p, tStamp: %p\n", iface, sPos, tStamp);

    if (!sPos || !tStamp)
        return ASE_InvalidParameter;

    *tStamp = ASIO_LONG(ASIOTimeStamp, This->asio_time_stamp);
    *sPos = ASIO_LONG(ASIOSamples, This->asio_sample_position);

    return ASE_OK;
}

/*
 * ASIOError GetChannelInfo (ASIOChannelInfo *info);
 *  Function:   Retrive channel info. - See asio.h for more detail
 *  Returns:    ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(GetChannelInfo,8)
HIDDEN ASIOError STDMETHODCALLTYPE GetChannelInfo(LPWINEASIO iface, ASIOChannelInfo *info)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("(iface: %p, info: %p\n", iface, info);

    if (info->channel < 0 || (info->isInput ? info->channel >= This->wineasio_number_inputs : info->channel >= This->wineasio_number_outputs))
        return ASE_InvalidParameter;

    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB;

    if (info->isInput)
    {
        info->isActive = This->input_channel[info->channel].active;
        memcpy(info->name, This->input_channel[info->channel].port_name, ASIO_MAX_NAME_LENGTH);
    }
    else
    {
        info->isActive = This->output_channel[info->channel].active;
        memcpy(info->name, This->output_channel[info->channel].port_name, ASIO_MAX_NAME_LENGTH);
    }
    return ASE_OK;
}

/*
 * ASIOError CreateBuffers(ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks);
 *  Function:   Allocate buffers for IO channels
 *  Parameters: bufferInfo      - pointer to an array of ASIOBufferInfo structures
 *              numChannels     - the total number of IO channels to be allocated
 *              bufferSize      - one of the buffer sizes retrieved with ASIOGetBufferSize()
 *              asioCallbacks   - pointer to an ASIOCallbacks structure
 *              See asio.h for more detail
 *  Returns:    ASE_NoMemory if impossible to allocate enough memory
 *              ASE_InvalidMode on unsupported bufferSize or invalid bufferInfo data
 *              ASE_NotPresent on missing IO
 */

DEFINE_THISCALL_WRAPPER(CreateBuffers,20)
HIDDEN ASIOError STDMETHODCALLTYPE CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfo, LONG numChannels, LONG bufferSize, ASIOCallbacks *asioCallbacks)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    ASIOBufferInfo  *buffer_info = bufferInfo;
    int             i;

    TRACE("iface: %p, driver state: %d, bufferInfo: %p, numChannels: %i, bufferSize: %i, asioCallbacks: %p\n", iface, This->asio_driver_state, bufferInfo, (int)numChannels, (int)bufferSize, asioCallbacks);

    if (This->asio_driver_state != Initialized) {
        set_last_error("Driver is not initialized");
        return ASE_NotPresent;
    }

    if (!bufferInfo || !asioCallbacks) {
        set_last_error("Invalid buffer creation parameters");
        return ASE_InvalidMode;
    }

    /* set buf_size - always use our configured buffer size */
    if (This->wineasio_fixed_buffersize)
    {
        if (This->asio_current_buffersize != bufferSize)
        {
            TRACE("ASIO application requested %d samples, but driver is configured for %d samples\n", 
                  (int)bufferSize, (int)This->asio_current_buffersize);
            /* Force the application to use our configured buffer size */
        }
        TRACE("Buffersize fixed at %i\n", (int)This->asio_current_buffersize);
    }
    else
    { /* fail if out of range */
        if (!(bufferSize >= ASIO_MINIMUM_BUFFERSIZE
            && bufferSize <= ASIO_MAXIMUM_BUFFERSIZE))
        {
            WARN("Invalid buffersize %i requested\n", (int)bufferSize);
            set_last_error("Invalid buffer size %d requested", (int)bufferSize);
            return ASE_InvalidMode;
        }

        if (This->asio_current_buffersize == bufferSize)
        {
            TRACE("Buffer size already set to %i\n", (int)This->asio_current_buffersize);
        }
        else
        {
            /* Use our configured buffer size instead of what the application requested */
            TRACE("Buffer size forced to configured value: %i (requested %i)\n",
                  (int)This->asio_current_buffersize, (int)bufferSize);
        }
    }

    /* print/discover ASIO host capabilities */
    This->asio_callbacks = asioCallbacks;
    This->asio_time_info_mode = This->asio_can_time_code = FALSE;

    TRACE("The ASIO host supports ASIO v%i: ", This->asio_callbacks->asioMessage(kAsioEngineVersion, 0, 0, 0));
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioBufferSizeChange, 0 , 0))
        TRACE("kAsioBufferSizeChange ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResetRequest, 0 , 0))
        TRACE("kAsioResetRequest ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResyncRequest, 0 , 0))
        TRACE("kAsioResyncRequest ");
    if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioLatenciesChanged, 0 , 0))
        TRACE("kAsioLatenciesChanged ");

    if (This->asio_callbacks->asioMessage(kAsioSupportsTimeInfo, 0, 0, 0))
    {
        TRACE("bufferSwitchTimeInfo ");
        This->asio_time_info_mode = TRUE;
        if (This->asio_callbacks->asioMessage(kAsioSupportsTimeCode,  0, 0, 0))
        {
            TRACE("TimeCode");
            This->asio_can_time_code = TRUE;
        }
    }
    else
        TRACE("BufferSwitch");
    TRACE("\n");

    /* initialize ASIOBufferInfo structures */
    buffer_info = bufferInfo;
    This->asio_active_inputs = This->asio_active_outputs = 0;

    for (i = 0; i < numChannels; i++, buffer_info++)
    {
        IOChannel *chan;
        if (buffer_info->isInput)
        {
            This->asio_active_inputs++;
            chan = &This->input_channel[buffer_info->channelNum];
        }
        else
        {
            This->asio_active_outputs++;
            chan = &This->output_channel[buffer_info->channelNum];
        }

        chan->active = true;
        chan->buffers[0] = NULL;
        chan->buffers[1] = NULL;
    }

    /* Connect PipeWire filter only if not already connected */
    if (This->pw_filter && pw_filter_get_state(This->pw_filter, NULL) == PW_FILTER_STATE_UNCONNECTED) {
        /* Set quantum to match ASIO buffer size */
        char rate_str[32], latency_str[32];
        uint32_t quantum = This->asio_current_buffersize;
        uint32_t rate = (uint32_t)This->asio_sample_rate;

        snprintf(rate_str, sizeof(rate_str), "%u/%u", quantum, rate);
        snprintf(latency_str, sizeof(latency_str), "%u/%u", quantum, rate);

        printf("Setting PipeWire filter quantum to %u samples (%.2f ms @ %u Hz)\n",
               quantum, (double)quantum * 1000.0 / rate, rate);

        const struct spa_dict_item items[] = {
            { PW_KEY_NODE_RATE, rate_str },
            { PW_KEY_NODE_LATENCY, latency_str },
        };
        const struct spa_dict dict = SPA_DICT_INIT(items, 2);
        pw_filter_update_properties(This->pw_filter, NULL, &dict);

        TRACE("Connecting filter with %d samples quantum\n", This->asio_current_buffersize);

        char pod_buffer[0x1000];
        struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
        struct spa_audio_info_raw format = SPA_AUDIO_INFO_RAW_INIT(
            .format = SPA_AUDIO_FORMAT_F32,
            .rate = This->asio_sample_rate,
            .channels = This->asio_active_outputs,
        );
        
        /* Calculate latency in nanoseconds from buffer size and sample rate */
        uint64_t latency_ns = (uint64_t)This->asio_current_buffersize * SPA_NSEC_PER_SEC / (uint64_t)This->asio_sample_rate;
        
        /* Add timing constraints to ensure consistent buffer sizes */
        struct spa_pod const *connect_params[] = {
            spa_format_audio_raw_build(&pod_builder, SPA_PARAM_EnumFormat, &format),
            spa_process_latency_build(&pod_builder, SPA_PARAM_ProcessLatency,
                &SPA_PROCESS_LATENCY_INFO_INIT(.ns = latency_ns)),
            spa_pod_builder_add_object(&pod_builder,
                SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(2),
                SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
                SPA_PARAM_BUFFERS_size, SPA_POD_Int(This->asio_current_buffersize * sizeof(float)),
                SPA_PARAM_BUFFERS_stride, SPA_POD_Int(sizeof(float))
            ),
        };
        
        TRACE("Connecting PipeWire filter with rate=%f, channels=%d\n", This->asio_sample_rate, This->asio_active_outputs);
        TRACE("Connecting PipeWire filter with %d samples (%.2f ms) at %.0f Hz\n", 
              This->asio_current_buffersize, 
              (double)This->asio_current_buffersize * 1000.0 / This->asio_sample_rate,
              This->asio_sample_rate);
        
        user_pw_lock_loop(This->pw_helper);
        if (pw_filter_connect(This->pw_filter, PW_FILTER_FLAG_RT_PROCESS, connect_params, ARRAYSIZE(connect_params)) < 0) {
            user_pw_unlock_loop(This->pw_helper);
            ERR("Failed to connect PipeWire filter\n");
            set_last_error("Failed to connect PipeWire filter");
            return ASE_HWMalfunction;
        }
        user_pw_unlock_loop(This->pw_helper);
        
        TRACE("PipeWire filter connected successfully\n");
    } else if (This->pw_filter) {
        TRACE("PipeWire filter already connected, state: %s\n", 
              pw_filter_state_as_string(pw_filter_get_state(This->pw_filter, NULL)));
    }

    /* Wait for filter to reach paused state - use longer timeout for small buffer sizes */
    int paused_timeout = (bufferSize <= 128) ? 15000 : 10000;
    TRACE("Waiting for PipeWire filter to reach paused state (timeout: %d ms)...\n", paused_timeout);
    if (!user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_PAUSED, paused_timeout)) {
        ERR("Timeout waiting for PipeWire filter to reach paused state\n");
        set_last_error("Timeout waiting for PipeWire filter to reach paused state");
        return ASE_HWMalfunction;
    }
    TRACE("PipeWire filter successfully reached paused state\n");

    buffer_info = bufferInfo;
    for (i = 0; i < numChannels; i++, buffer_info++)
    {
        IOChannel *chan;
        if (buffer_info->isInput)
        {
            chan = &This->input_channel[buffer_info->channelNum];
            /* TRACE("ASIO audio buffer for channel %i as input %li created\n", i, This->asio_active_inputs); */
        }
        else
        {
            chan = &This->output_channel[buffer_info->channelNum];
            /* TRACE("ASIO audio buffer for channel %i as output %li created\n", i, This->asio_active_outputs); */
        }

        TRACE("Channel idx %d: buffer 0: %p, buffer 1: %p\n", i, chan->buffers[0], chan->buffers[1]);
        
        /* Allocate Wine-compatible buffers with optimal alignment for performance */
        chan->buffer_size = bufferSize * sizeof(float);
        
        /* Align to cache line boundaries and ensure power-of-2 alignment for SIMD */
        size_t aligned_size = ALIGN_TO_CACHE_LINE(chan->buffer_size);
        
        chan->wine_buffers[0] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, aligned_size);
        chan->wine_buffers[1] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, aligned_size);
        
        if (!chan->wine_buffers[0] || !chan->wine_buffers[1]) {
            if (chan->wine_buffers[0]) {
                HeapFree(GetProcessHeap(), 0, chan->wine_buffers[0]);
                chan->wine_buffers[0] = NULL;
            }
            if (chan->wine_buffers[1]) {
                HeapFree(GetProcessHeap(), 0, chan->wine_buffers[1]);
                chan->wine_buffers[1] = NULL;
            }
            ERR("Failed to allocate Wine-compatible buffers for channel %d\n", i);
            set_last_error("Failed to allocate audio buffers");
            return ASE_NoMemory;
        }
        
        /* Always use copying for stability - direct buffer access can cause issues */
        chan->needs_copy = true;
        
        TRACE("Channel %d: Allocated %zu byte buffers (aligned from %zu)\n", i, aligned_size, chan->buffer_size);
        
        /* Provide Wine-allocated buffers to ASIO application */
        buffer_info->buffers[0] = chan->wine_buffers[0];
        buffer_info->buffers[1] = chan->wine_buffers[1];
        
        TRACE("Channel %d: Wine buffers allocated at %p, %p (size=%zu)\n", 
              i, chan->wine_buffers[0], chan->wine_buffers[1], chan->buffer_size);
    }
    TRACE("%i audio channels initialized\n", This->asio_active_inputs + This->asio_active_outputs);

    /* Ensure all newly allocated buffers are clean for consistent initial state */
    clear_audio_buffers(This, "buffer creation");

    /* Initialize ASIO callback manager for Wine thread marshalling */
    if (!init_asio_callback_manager(This)) {
        ERR("Failed to initialize ASIO callback manager\n");
        set_last_error("Failed to initialize ASIO callback manager");
        return ASE_HWMalfunction;
    }

    /* at this point all the connections are made and the PipeWire process callback is outputting silence */
    This->asio_driver_state = Prepared;
    return ASE_OK;
}

/*
 * ASIOError DisposeBuffers(void);
 *  Function:   Release allocated buffers
 *  Returns:    ASE_InvalidMode if no buffers were previously allocated
 *              ASE_NotPresent on missing IO
 *  Implies:    ASIOStop()
 */

DEFINE_THISCALL_WRAPPER(DisposeBuffers,4)
HIDDEN ASIOError STDMETHODCALLTYPE DisposeBuffers(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;
    int             i;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state == Running)
        Stop (iface);
    if (This->asio_driver_state != Prepared) {
        set_last_error("Driver is not prepared");
        return ASE_NotPresent;
    }

    /* Properly disconnect and destroy the PipeWire filter */
    if (This->pw_filter) {
        user_pw_lock_loop(This->pw_helper);
        pw_filter_disconnect(This->pw_filter);
        user_pw_unlock_loop(This->pw_helper);
        
        /* Wait for filter to reach disconnected state */
        user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_UNCONNECTED, 5000);
        
        pw_filter_destroy(This->pw_filter);
        This->pw_filter = NULL;
        TRACE("PipeWire filter properly disconnected and destroyed\n");
    }

    This->asio_callbacks = NULL;

    for (i = 0; i < This->wineasio_number_inputs; i++)
    {
        This->input_channel[i].buffers[0] = NULL;
        This->input_channel[i].buffers[1] = NULL;
        
        /* Free Wine-allocated buffers */
        if (This->input_channel[i].wine_buffers[0]) {
            HeapFree(GetProcessHeap(), 0, This->input_channel[i].wine_buffers[0]);
            This->input_channel[i].wine_buffers[0] = NULL;
        }
        if (This->input_channel[i].wine_buffers[1]) {
            HeapFree(GetProcessHeap(), 0, This->input_channel[i].wine_buffers[1]);
            This->input_channel[i].wine_buffers[1] = NULL;
        }
        
        This->input_channel[i].active = false;
    }
    for (i = 0; i < This->wineasio_number_outputs; i++)
    {
        This->output_channel[i].buffers[0] = NULL;
        This->output_channel[i].buffers[1] = NULL;
        
        /* Free Wine-allocated buffers */
        if (This->output_channel[i].wine_buffers[0]) {
            HeapFree(GetProcessHeap(), 0, This->output_channel[i].wine_buffers[0]);
            This->output_channel[i].wine_buffers[0] = NULL;
        }
        if (This->output_channel[i].wine_buffers[1]) {
            HeapFree(GetProcessHeap(), 0, This->output_channel[i].wine_buffers[1]);
            This->output_channel[i].wine_buffers[1] = NULL;
        }
        
        This->output_channel[i].active = false;
    }
    This->asio_active_inputs = This->asio_active_outputs = 0;

    if (This->midi_input_ports) {
        for (i = 0; i < This->wineasio_number_midi_inputs; i++) {
            This->midi_input_ports[i].active = false;
            This->midi_input_ports[i].port = NULL;
        }
    }
    if (This->midi_output_ports) {
        for (i = 0; i < This->wineasio_number_midi_outputs; i++) {
            This->midi_output_ports[i].active = false;
            This->midi_output_ports[i].port = NULL;
        }
    }
    if (This->midi_buffers_initialized) {
        midi_buffer_clear(&This->midi_in_buffer);
        midi_buffer_clear(&This->midi_out_buffer);
    }

    /* Cleanup ASIO callback manager */
    cleanup_asio_callback_manager();

    This->asio_driver_state = Initialized;
    return ASE_OK;
}

/*
 * ASIOError ControlPanel(void);
 *  Function:   Open a control panel for driver settings
 *  Returns:    ASE_NotPresent if no control panel exists.  Actually return code should be ignored
 *  Note:       Call the asioMessage callback if something has changed
 */

DEFINE_THISCALL_WRAPPER(ControlPanel,4)
HIDDEN ASIOError STDMETHODCALLTYPE ControlPanel(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)iface;

    TRACE("Opening control panel\n");

    if (This->gui == NULL) {
        This->gui = pwasio_init_gui(&This->gui_conf);
    }
    return ASE_OK;
}

HIDDEN void GuiClosed(struct pwasio_gui_conf *conf)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)conf->user;
    pwasio_destroy_gui(This->gui);
    This->gui = NULL;
}

HIDDEN void GuiApplyConfig(struct pwasio_gui_conf *conf)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)conf->user;
    
    TRACE("Applying GUI configuration changes\n");
    TRACE("GUI config: state=%d, buffer=%ld -> %u\n", This->asio_driver_state,
          This->wineasio_preferred_buffersize, conf->cf_buffer_size);
    
    /* Update buffer size preference and current buffer size */
    if (This->wineasio_preferred_buffersize != conf->cf_buffer_size) {
        TRACE("Changing buffer size from %ld to %u\n", This->wineasio_preferred_buffersize, conf->cf_buffer_size);
        This->wineasio_preferred_buffersize = conf->cf_buffer_size;
        
        /* Also update the current buffer size if driver is not in a prepared/running state */
        if (This->asio_driver_state == Initialized || This->asio_driver_state == Loaded) {
            This->asio_current_buffersize = conf->cf_buffer_size;
            TRACE("Updated current buffer size to %u (driver not prepared)\n", conf->cf_buffer_size);
        } else if (This->asio_driver_state == Prepared || This->asio_driver_state == Running) {
            /* Update current buffer size immediately */
            This->asio_current_buffersize = conf->cf_buffer_size;
            TRACE("Updated current buffer size to %u (driver prepared/running)\n", conf->cf_buffer_size);
            
            /* If PipeWire filter is connected, reconnect it with the new buffer size */
            if (This->pw_filter && pw_filter_get_state(This->pw_filter, NULL) != PW_FILTER_STATE_UNCONNECTED) {
                TRACE("Reconnecting PipeWire filter with new buffer size\n");
                
                /* Disconnect the current filter */
                user_pw_lock_loop(This->pw_helper);
                pw_filter_disconnect(This->pw_filter);
                user_pw_unlock_loop(This->pw_helper);
                
                /* Wait for disconnection */
                user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_UNCONNECTED, 5000);
                
                /* Reconnect with new buffer size */
                char pod_buffer[0x1000];
                struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof pod_buffer);
                struct spa_audio_info_raw format = SPA_AUDIO_INFO_RAW_INIT(
                    .format = SPA_AUDIO_FORMAT_F32,
                    .rate = This->asio_sample_rate,
                    .channels = This->asio_active_outputs,
                );
                
                /* Calculate latency in nanoseconds from new buffer size and sample rate */
                uint64_t latency_ns = (uint64_t)This->asio_current_buffersize * SPA_NSEC_PER_SEC / (uint64_t)This->asio_sample_rate;
                
                /* Add timing constraints to ensure consistent buffer sizes */
                struct spa_pod const *connect_params[] = {
                    spa_format_audio_raw_build(&pod_builder, SPA_PARAM_EnumFormat, &format),
                    spa_process_latency_build(&pod_builder, SPA_PARAM_ProcessLatency,
                        &SPA_PROCESS_LATENCY_INFO_INIT(.ns = latency_ns)),
                    spa_pod_builder_add_object(&pod_builder,
                        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                        SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(2),
                        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
                        SPA_PARAM_BUFFERS_size, SPA_POD_Int(This->asio_current_buffersize * sizeof(float)),
                        SPA_PARAM_BUFFERS_stride, SPA_POD_Int(sizeof(float))
                    ),
                };
                
                TRACE("GUI: Reconnecting PipeWire with new quantum: %d samples (%.2f ms) at %.0f Hz\n",
                      This->asio_current_buffersize,
                      (double)This->asio_current_buffersize * 1000.0 / This->asio_sample_rate,
                      This->asio_sample_rate);

                /* Set quantum to match new ASIO buffer size */
                char rate_str[32], latency_str[32];
                uint32_t quantum = This->asio_current_buffersize;
                uint32_t rate = (uint32_t)This->asio_sample_rate;
                snprintf(rate_str, sizeof(rate_str), "%u/%u", quantum, rate);
                snprintf(latency_str, sizeof(latency_str), "%u/%u", quantum, rate);

                user_pw_lock_loop(This->pw_helper);
                const struct spa_dict_item items2[] = {
                    { PW_KEY_NODE_RATE, rate_str },
                    { PW_KEY_NODE_LATENCY, latency_str },
                };
                const struct spa_dict dict2 = SPA_DICT_INIT(items2, 2);
                pw_filter_update_properties(This->pw_filter, NULL, &dict2);

                if (pw_filter_connect(This->pw_filter, PW_FILTER_FLAG_RT_PROCESS, connect_params, ARRAYSIZE(connect_params)) < 0) {
                    user_pw_unlock_loop(This->pw_helper);
                    ERR("Failed to reconnect PipeWire filter with new buffer size\n");
                    set_last_error("Failed to reconnect PipeWire filter with new buffer size");
                } else {
                    user_pw_unlock_loop(This->pw_helper);
                    
                    /* Wait for filter to reach paused state */
                    if (user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_PAUSED, 10000)) {
                        TRACE("PipeWire filter successfully reconnected with new buffer size\n");
                    } else {
                        ERR("Timeout waiting for PipeWire filter to reach paused state after reconnection\n");
                        set_last_error("Timeout waiting for PipeWire filter after buffer size change");
                    }
                }
            } else {
                TRACE("PipeWire filter not connected or already disconnected\n");
            }
            
            /* Buffers are already created, need to notify host about buffer size change */
            if (This->asio_callbacks && This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioBufferSizeChange, 0, 0)) {
                TRACE("Requesting ASIO buffer size change notification\n");
                This->asio_callbacks->asioMessage(kAsioBufferSizeChange, 0, 0, 0);
            } else {
                TRACE("ASIO callbacks not available or buffer size change not supported\n");
            }
        }
    } else {
        TRACE("GUI: Buffer size unchanged (%u samples)\n", conf->cf_buffer_size);
    }
    
    /* Apply sample rate change if different */
    if (This->asio_sample_rate != conf->cf_sample_rate) {
        TRACE("Changing sample rate from %.0f to %u\n", This->asio_sample_rate, conf->cf_sample_rate);
        This->asio_sample_rate = conf->cf_sample_rate;
        
        /* If PipeWire filter is connected, reconnect it with the new sample rate */
        if (This->pw_filter && pw_filter_get_state(This->pw_filter, NULL) != PW_FILTER_STATE_UNCONNECTED) {
            TRACE("Reconnecting PipeWire filter with new sample rate\n");
            
            /* Disconnect the current filter */
            user_pw_lock_loop(This->pw_helper);
            pw_filter_disconnect(This->pw_filter);
            user_pw_unlock_loop(This->pw_helper);
            
            /* Wait for disconnection */
            user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_UNCONNECTED, 5000);
            
            /* Reconnect with new sample rate */
            char pod_buffer[0x1000];
            struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof pod_buffer);
            struct spa_audio_info_raw format = SPA_AUDIO_INFO_RAW_INIT(
                .format = SPA_AUDIO_FORMAT_F32,
                .rate = This->asio_sample_rate,
                .channels = This->asio_active_outputs,
            );
            
            /* Calculate latency in nanoseconds from current buffer size and new sample rate */
            uint64_t latency_ns = (uint64_t)This->asio_current_buffersize * SPA_NSEC_PER_SEC / (uint64_t)This->asio_sample_rate;
            
            /* Add timing constraints to ensure consistent buffer sizes */
            struct spa_pod const *connect_params[] = {
                spa_format_audio_raw_build(&pod_builder, SPA_PARAM_EnumFormat, &format),
                spa_process_latency_build(&pod_builder, SPA_PARAM_ProcessLatency,
                    &SPA_PROCESS_LATENCY_INFO_INIT(.ns = latency_ns)),
                spa_pod_builder_add_object(&pod_builder,
                    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                    SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(2),
                    SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
                    SPA_PARAM_BUFFERS_size, SPA_POD_Int(This->asio_current_buffersize * sizeof(float)),
                    SPA_PARAM_BUFFERS_stride, SPA_POD_Int(sizeof(float))
                ),
            };
            
            TRACE("GUI: Reconnecting PipeWire with new sample rate: %.0f Hz (buffer: %d samples)\n",
                  This->asio_sample_rate, This->asio_current_buffersize);

            /* Set quantum to match ASIO buffer size with new sample rate */
            char rate_str[32], latency_str[32];
            uint32_t quantum = This->asio_current_buffersize;
            uint32_t rate = (uint32_t)This->asio_sample_rate;
            snprintf(rate_str, sizeof(rate_str), "%u/%u", quantum, rate);
            snprintf(latency_str, sizeof(latency_str), "%u/%u", quantum, rate);

            const struct spa_dict_item items3[] = {
                { PW_KEY_NODE_RATE, rate_str },
                { PW_KEY_NODE_LATENCY, latency_str },
            };
            const struct spa_dict dict3 = SPA_DICT_INIT(items3, 2);

            user_pw_lock_loop(This->pw_helper);
            pw_filter_update_properties(This->pw_filter, NULL, &dict3);

            if (pw_filter_connect(This->pw_filter, PW_FILTER_FLAG_RT_PROCESS, connect_params, ARRAYSIZE(connect_params)) < 0) {
                user_pw_unlock_loop(This->pw_helper);
                ERR("Failed to reconnect PipeWire filter with new sample rate\n");
                set_last_error("Failed to reconnect PipeWire filter with new sample rate");
            } else {
                user_pw_unlock_loop(This->pw_helper);
                
                /* Wait for filter to reach paused state */
                if (user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter, PW_FILTER_STATE_PAUSED, 10000)) {
                    TRACE("PipeWire filter successfully reconnected with new sample rate\n");
                } else {
                    ERR("Timeout waiting for PipeWire filter to reach paused state after sample rate change\n");
                    set_last_error("Timeout waiting for PipeWire filter after sample rate change");
                }
            }
        } else {
            TRACE("PipeWire filter not connected, sample rate will be applied on next connection\n");
        }
        
        /* If the driver is running, notify the ASIO host about the sample rate change */
        if (This->asio_driver_state == Running && This->asio_callbacks) {
        if (This->asio_callbacks->asioMessage(kAsioSelectorSupported, kAsioResetRequest, 0, 0)) {
            TRACE("Requesting ASIO reset due to sample rate change\n");
            This->asio_callbacks->asioMessage(kAsioResetRequest, 0, 0, 0);
        }

        }
    }
    
    /* Apply input/output channel changes */
    if (This->wineasio_number_inputs != conf->cf_input_channels) {
        TRACE("Changing input channels from %d to %u\n", This->wineasio_number_inputs, conf->cf_input_channels);
        This->wineasio_number_inputs = conf->cf_input_channels;
    }
    
    if (This->wineasio_number_outputs != conf->cf_output_channels) {
        TRACE("Changing output channels from %d to %u\n", This->wineasio_number_outputs, conf->cf_output_channels);
        This->wineasio_number_outputs = conf->cf_output_channels;
    }
    
    /* Apply auto-connect setting */
    This->wineasio_connect_to_hardware = conf->cf_auto_connect;

    /* Save the updated configuration to file */
    configure_driver(This);
    
    TRACE("GUI configuration applied successfully\n");
}

HIDDEN void GuiLoadConfig(struct pwasio_gui_conf *conf)
{
    IWineASIOImpl   *This = (IWineASIOImpl *)conf->user;
    
    TRACE("Loading configuration for GUI\n");
    
    conf->cf_buffer_size = This->wineasio_preferred_buffersize;
    conf->cf_sample_rate = (uint32_t)This->asio_sample_rate;
    conf->cf_input_channels = This->wineasio_number_inputs;
    conf->cf_output_channels = This->wineasio_number_outputs;
    conf->cf_auto_connect = This->wineasio_connect_to_hardware;
    
    TRACE("Loaded config: buffer_size=%u, sample_rate=%u, inputs=%u, outputs=%u, auto_connect=%s\n",
          conf->cf_buffer_size, conf->cf_sample_rate, conf->cf_input_channels, 
          conf->cf_output_channels, conf->cf_auto_connect ? "true" : "false");
}

/*
 * ASIOError Future(LONG selector, void *opt);
 *  Function:   Various, See asio.h for more detail
 *  Returns:    Depends on the selector but in general ASE_InvalidParameter on invalid selector
 *              ASE_InvalidParameter if function is unsupported to disable further calls
 *              ASE_SUCCESS on success, do not use AES_OK
 */

DEFINE_THISCALL_WRAPPER(Future,12)
HIDDEN ASIOError STDMETHODCALLTYPE Future(LPWINEASIO iface, LONG selector, void *opt)
{
    IWineASIOImpl           *This = (IWineASIOImpl *) iface;

    TRACE("iface: %p, selector: %i, opt: %p\n", iface, selector, opt);

    switch (selector)
    {
        case PW_ASIO_MIDI_SELECTOR_GET_INFO: {
            if (!opt) {
                return ASE_InvalidParameter;
            }
            PWASIO_MidiInfo *info = (PWASIO_MidiInfo *)opt;
            info->api_version = PW_ASIO_MIDI_API_VERSION;
            info->num_inputs = (uint32_t)This->wineasio_number_midi_inputs;
            info->num_outputs = (uint32_t)This->wineasio_number_midi_outputs;
            return ASE_SUCCESS;
        }
        case PW_ASIO_MIDI_SELECTOR_GET_INPUT: {
            if (!opt || !This->midi_buffers_initialized || This->wineasio_number_midi_inputs <= 0) {
                return ASE_InvalidParameter;
            }
            PWASIO_MidiBuffer *buf = (PWASIO_MidiBuffer *)opt;
            if (!buf->events || buf->capacity == 0) {
                buf->count = 0;
                return ASE_InvalidParameter;
            }

            uint32_t read_count = 0;
            struct midi_event ev;
            while (read_count < buf->capacity && midi_buffer_read(&This->midi_in_buffer, &ev)) {
                PWASIO_MidiEvent *out = &buf->events[read_count];
                out->offset = ev.offset;
                out->size = ev.size;
                out->port = ev.port;
                memset(out->reserved, 0, sizeof(out->reserved));
                memset(out->data, 0, sizeof(out->data));
                if (ev.size > 0) {
                    memcpy(out->data, ev.data, ev.size);
                }
                read_count++;
            }
            buf->count = read_count;
            return ASE_SUCCESS;
        }
        case PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT: {
            if (!opt || !This->midi_buffers_initialized || This->wineasio_number_midi_outputs <= 0) {
                return ASE_InvalidParameter;
            }
            PWASIO_MidiBuffer *buf = (PWASIO_MidiBuffer *)opt;
            if (!buf->events || buf->count == 0) {
                buf->count = 0;
                return ASE_SUCCESS;
            }

            uint32_t accepted = 0;
            for (uint32_t i = 0; i < buf->count; ++i) {
                PWASIO_MidiEvent *ev = &buf->events[i];
                if (ev->port >= (uint8_t)This->wineasio_number_midi_outputs) {
                    continue;
                }
                if (!midi_buffer_write(&This->midi_out_buffer, ev->port, ev->offset, ev->data, ev->size)) {
                    break;
                }
                accepted++;
            }

            buf->count = accepted;
            return ASE_SUCCESS;
        }
        case kAsioEnableTimeCodeRead:
            This->asio_can_time_code = TRUE;
            TRACE("The ASIO host enabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioDisableTimeCodeRead:
            This->asio_can_time_code = FALSE;
            TRACE("The ASIO host disabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioSetInputMonitor:
            TRACE("The driver denied request to set input monitor\n");
            return ASE_NotPresent;
        case kAsioTransport:
            TRACE("The driver denied request for ASIO Transport control\n");
            return ASE_InvalidParameter;
        case kAsioSetInputGain:
            TRACE("The driver denied request to set input gain\n");
            return ASE_InvalidParameter;
        case kAsioGetInputMeter:
            TRACE("The driver denied request to get input meter \n");
            return ASE_InvalidParameter;
        case kAsioSetOutputGain:
            TRACE("The driver denied request to set output gain\n");
            return ASE_InvalidParameter;
        case kAsioGetOutputMeter:
            TRACE("The driver denied request to get output meter\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMonitor:
            TRACE("The driver does not support input monitor\n");
            return ASE_InvalidParameter;
        case kAsioCanTimeInfo:
            TRACE("The driver supports TimeInfo\n");
            return ASE_SUCCESS;
        case kAsioCanTimeCode:
            TRACE("The driver supports TimeCode\n");
            return ASE_SUCCESS;
        case kAsioCanTransport:
            TRACE("The driver denied request for ASIO Transport\n");
            return ASE_InvalidParameter;
        case kAsioCanInputGain:
            TRACE("The driver does not support input gain\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMeter:
            TRACE("The driver does not support input meter\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputGain:
            TRACE("The driver does not support output gain\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputMeter:
            TRACE("The driver does not support output meter\n");
            return ASE_InvalidParameter;
        case kAsioSetIoFormat:
            TRACE("The driver denied request to set DSD IO format\n");
            return ASE_NotPresent;
        case kAsioGetIoFormat:
            TRACE("The driver denied request to get DSD IO format\n");
            return ASE_NotPresent;
        case kAsioCanDoIoFormat:
            TRACE("The driver does not support DSD IO format\n");
            return ASE_NotPresent;
        default:
            TRACE("ASIOFuture() called with undocumented selector\n");
            return ASE_InvalidParameter;
    }
}

/*
 * ASIOError OutputReady(void);
 *  Function:   Tells the driver that output bufffers are ready
 *  Returns:    ASE_OK if supported
 *              ASE_NotPresent to disable
 */

DEFINE_THISCALL_WRAPPER(OutputReady,4)
HIDDEN ASIOError STDMETHODCALLTYPE OutputReady(LPWINEASIO iface)
{
    /* disabled to stop stand alone NI programs from spamming the console
    TRACE("iface: %p\n", iface); */
    return ASE_NotPresent;
}

/****************************************************************************
 *  Support functions
 */


/*****************************************************************************
 *  Support functions
 */

#ifndef WINE_WITH_UNICODE
/* Funtion required as unicode.h no longer in WINE */
static WCHAR *strrchrW(const WCHAR* str, WCHAR ch)
{
    WCHAR *ret = NULL;
    do { if (*str == ch) ret = (WCHAR *)(ULONG_PTR)str; } while (*str++);
    return ret;
}
#endif

/* Function called by PipeWire to create a thread in the wine process context,
 *  uses the global structure jack_thread_creator_privates to communicate with jack_thread_creator_helper() */
static int jack_thread_creator(pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg)
{
    TRACE("arg: %p, thread_id: %p, attr: %p, function: %p\n", arg, thread_id, attr, function);

    jack_thread_creator_privates.jack_callback_thread = function;
    jack_thread_creator_privates.arg = arg;
    jack_thread_creator_privates.jack_callback_thread_created = CreateEventW(NULL, FALSE, FALSE, NULL);
    CreateThread(NULL, 0, jack_thread_creator_helper, arg, 0, 0);
    WaitForSingleObject(jack_thread_creator_privates.jack_callback_thread_created, INFINITE);
    *thread_id = jack_thread_creator_privates.jack_callback_pthread_id;
    return 0;
}

/* internal helper function for returning the posix thread_id of the newly created callback thread */
static DWORD WINAPI jack_thread_creator_helper(LPVOID arg)
{
    TRACE("arg: %p\n", arg);

    jack_thread_creator_privates.jack_callback_pthread_id = pthread_self();
    SetEvent(jack_thread_creator_privates.jack_callback_thread_created);
    jack_thread_creator_privates.jack_callback_thread(jack_thread_creator_privates.arg);
    return 0;
}

static void get_nodes_by_name(IWineASIOImpl *This) {
    char *namebuf = NULL;
    int namebuf_len = 0;
    int required_len;

    This->current_input_node = NULL;
    This->current_output_node = NULL;

    if (This->pwasio_input_device_name[0]) {
        required_len = WideCharToMultiByte(CP_UTF8, 0, This->pwasio_input_device_name, -1, NULL, 0, NULL, NULL);
        if (required_len == 0) {
            ERR("Failed to convert input device name to UTF-8\n");
        } else {
            if (namebuf_len < required_len) {
                free(namebuf);
                namebuf = malloc(required_len);
                if (!namebuf) {
                    ERR("Failed to allocate memory for input device name\n");
                    set_last_error("Failed to allocate memory for input device name");
                    return;
                }
                namebuf_len = required_len;
            }
            if (0 == WideCharToMultiByte(CP_UTF8, 0, This->pwasio_input_device_name, -1, namebuf, namebuf_len, NULL, NULL)) {
                ERR("Failed to convert input device name to UTF-8\n");
                set_last_error("Failed to convert input device name to UTF-8");
                free(namebuf);
                return;
            }
            This->current_input_node = user_pw_find_node_by_name(This->pw_helper, namebuf);
        }
    }

    if (This->pwasio_output_device_name[0]) {
        required_len = WideCharToMultiByte(CP_UTF8, 0, This->pwasio_output_device_name, -1, NULL, 0, NULL, NULL);
        if (required_len == 0) {
            ERR("Failed to convert output device name to UTF-8\n");
        } else {
            if (namebuf_len < required_len) {
                free(namebuf);
                namebuf = malloc(required_len);
                if (!namebuf) {
                    ERR("Failed to allocate memory for output device name\n");
                    set_last_error("Failed to allocate memory for output device name");
                    return;
                }
                namebuf_len = required_len;
            }
            if (0 == WideCharToMultiByte(CP_UTF8, 0, This->pwasio_output_device_name, -1, namebuf, namebuf_len, NULL, NULL)) {
                ERR("Failed to convert output device name to UTF-8\n");
                set_last_error("Failed to convert output device name to UTF-8");
                free(namebuf);
                return;
            }
            This->current_output_node = user_pw_find_node_by_name(This->pw_helper, namebuf);
        }
    }

    free(namebuf);

    if (This->current_input_node == NULL) {
        This->current_input_node = user_pw_get_default_node_nonblocking(This->pw_helper, SPA_DIRECTION_INPUT);
    }
    if (This->current_output_node == NULL) {
        This->current_output_node = user_pw_get_default_node_nonblocking(This->pw_helper, SPA_DIRECTION_OUTPUT);
    }
}

static void parse_boolean_env(char const *env, bool *var) {
    if (!env[0])
        return;
    if (!env[1]) {
        switch (env[0]) {
            case 'n': case 'N': case 'f': case 'F':
            case '0': *var = false; break;
            case 'y': case 'Y': case 't': case 'T':
            case '1': *var = true; break;
            default: ;
        }
        return;
    }

    if (!strcasecmp(env, "on") || !strcasecmp(env, "yes") || !strcasecmp(env, "true"))
        *var = true;
    else if (!strcasecmp(env, "off") || !strcasecmp(env, "no") || !strcasecmp(env, "false"))
        *var = false;
}

static int sanitize_channel_count(char const *label, int value, int default_value) {
    if (value <= 0 || value > PW_ASIO_MAX_CHANNELS) {
        WARN("Invalid %s channel count %d, using default %d\n", label, value, default_value);
        return default_value;
    }
    return value;
}

static int sanitize_midi_port_count(char const *label, int value, int default_value) {
    if (value < 0 || value > PW_ASIO_MAX_MIDI_PORTS) {
        WARN("Invalid %s MIDI port count %d, using default %d\n", label, value, default_value);
        return default_value;
    }
    return value;
}

/* Unicode strings used for the registry */
static const WCHAR key_software_wine_pwasio[] = u"Software\\Wine\\PipeWine";
static const WCHAR value_pwasio_number_inputs[] = u"Number of inputs";
static const WCHAR value_pwasio_number_outputs[] = u"Number of outputs";
static const WCHAR value_pwasio_buffersize_fixed[] = u"Use fixed buffer size";
static const WCHAR value_pwasio_buffersize[] = u"Buffer size";
static const WCHAR value_pwasio_connect_to_hardware[] = u"Connect to hardware";
static const WCHAR value_pwasio_input_device[] = u"Input device";
static const WCHAR value_pwasio_output_device[] = u"Output device";

static void configure_driver(IWineASIOImpl *This) {
    HKEY  hkey;
    LONG  result;
    LONG  status;
    DWORD size;
    DWORD type;
    DWORD value;
    WCHAR application_path[MAX_PATH];
    WCHAR *application_name;
    char environment_variable[MAX_ENVIRONMENT_SIZE];

    /* create registry entries with defaults if not present */
    result = RegCreateKeyExW(HKEY_CURRENT_USER, key_software_wine_pwasio, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (result != ERROR_SUCCESS) {
        ERR("Failed to open registry key for configuration (%d)\n", (int)result);
        set_last_error("Failed to open registry key for configuration");
        return;
    }


    /* get/set number of asio inputs */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_pwasio_number_inputs, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_number_inputs = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_number_inputs;
        result = RegSetValueExW(hkey, value_pwasio_number_inputs, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* get/set number of asio outputs */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_pwasio_number_outputs, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_number_outputs = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_number_outputs;
        result = RegSetValueExW(hkey, value_pwasio_number_outputs, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* allow changing of asio buffer sizes */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_pwasio_buffersize_fixed, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_fixed_buffersize = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_fixed_buffersize;
        result = RegSetValueExW(hkey, value_pwasio_buffersize_fixed, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* preferred buffer size (if changing buffersize is allowed) */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_pwasio_buffersize, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD) {
            TRACE("Registry override - changing buffer size from %d to %d\n", 
                  This->asio_current_buffersize, (int)value);
            This->wineasio_preferred_buffersize = value;
            /* Update current buffer size to match preferred size from registry */
            This->asio_current_buffersize = value;
        }
    }
    else
    {
        TRACE("No registry buffer size found, keeping config/default value: %d\n", 
              This->asio_current_buffersize);
        TRACE("Creating registry entry with current buffer size: %d\n", 
              This->wineasio_preferred_buffersize);
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_preferred_buffersize;
        result = RegSetValueExW(hkey, value_pwasio_buffersize, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* connect to hardware */
    size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_pwasio_connect_to_hardware, NULL, &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
            This->wineasio_connect_to_hardware = value;
    }
    else
    {
        type = REG_DWORD;
        size = sizeof(DWORD);
        value = This->wineasio_connect_to_hardware;
        result = RegSetValueExW(hkey, value_pwasio_connect_to_hardware, 0, REG_DWORD, (LPBYTE) &value, size);
    }

    /* input device name */
    This->pwasio_input_device_name[0] = 0;
    size = DEVICE_NAME_SIZE;
    status = RegQueryValueExW(hkey, value_pwasio_input_device, NULL, &type, (LPBYTE) &This->pwasio_input_device_name, &size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
    {
        if (type == REG_SZ) {
            if (size > DEVICE_NAME_SIZE - 1)
                size = DEVICE_NAME_SIZE - 1;

            This->pwasio_input_device_name[size] = 0;
        }
    }
    else
    {
        size = 0;
        result = RegSetValueExW(hkey, value_pwasio_input_device, 0, REG_SZ, (LPBYTE) &This->pwasio_input_device_name, size);
    }

    /* output device name */
    This->pwasio_output_device_name[0] = 0;
    size = DEVICE_NAME_SIZE;
    status = RegQueryValueExW(hkey, value_pwasio_output_device, NULL, &type, (LPBYTE) &This->pwasio_output_device_name, &size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
    {
        if (type == REG_SZ) {
            if (size > DEVICE_NAME_SIZE - 1)
                size = DEVICE_NAME_SIZE - 1;

            This->pwasio_output_device_name[size] = 0;
        }
    }
    else
    {
        size = 0;
        result = RegSetValueExW(hkey, value_pwasio_output_device, 0, REG_SZ, (LPBYTE) &This->pwasio_output_device_name, size);
    }

    /* override the PipeWire client name gotten from the application name */
    size = GetEnvironmentVariableW(u"PWASIO_CLIENT_NAME", application_path, ASIO_MAX_NAME_LENGTH);
    if (size == 0) {
        /* get client name by stripping path and extension */
        GetModuleFileNameW(0, application_path, MAX_PATH);
        application_name = strrchrW(application_path, L'.');
        *application_name = 0;
        application_name = strrchrW(application_path, L'\\');
        application_name++;
    } else {
        application_name = application_path;
    }

    WideCharToMultiByte(CP_UTF8, 0, application_name, -1, This->client_name, ASIO_MAX_NAME_LENGTH, NULL, NULL);

    RegCloseKey(hkey);

    /* Load configuration from PipeWire ASIO config file (overrides registry, but can be overridden by environment) */
    struct pw_helper_init_args config_args;
    const char *config_paths[] = {
        "/etc/pipewine/pipewine.conf",
        NULL,
        NULL
    };
    char user_config_path[512];
    
    /* Construct user config path */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(user_config_path, sizeof(user_config_path), 
                "%s/.config/pipewine/pipewine.conf", home);
        config_paths[1] = user_config_path;
    }
    
    /* Try to load configuration from files (user config takes precedence over system config) */
    bool config_loaded = false;
    for (int i = 1; i >= 0 && !config_loaded; i--) {
        if (config_paths[i] && pw_asio_load_config(&config_args, config_paths[i]) == 0) {
            config_loaded = true;
            
            /* Apply loaded settings (overrides registry values) */
            if (config_args.buffer_size >= ASIO_MINIMUM_BUFFERSIZE && 
                config_args.buffer_size <= ASIO_MAXIMUM_BUFFERSIZE) {
                This->wineasio_preferred_buffersize = config_args.buffer_size;
                This->asio_current_buffersize = config_args.buffer_size;
                TRACE("Loaded buffer size from config: %u\n", config_args.buffer_size);
                TRACE("Config override applied: preferred=%d, current=%d\n",
                      This->wineasio_preferred_buffersize, This->asio_current_buffersize);
            }
            
            if (config_args.sample_rate > 0) {
                This->asio_sample_rate = config_args.sample_rate;
                TRACE("Loaded sample rate from config: %u\n", config_args.sample_rate);
            }
            
            if (config_args.num_input_channels > 0 && config_args.num_input_channels <= 64) {
                This->wineasio_number_inputs = config_args.num_input_channels;
                TRACE("Loaded input channels from config: %u\n", config_args.num_input_channels);
            }
            
            if (config_args.num_output_channels > 0 && config_args.num_output_channels <= 64) {
                This->wineasio_number_outputs = config_args.num_output_channels;
                TRACE("Loaded output channels from config: %u\n", config_args.num_output_channels);
            }
            
            This->wineasio_connect_to_hardware = config_args.auto_connect;
            TRACE("Loaded auto-connect from config: %s\n", config_args.auto_connect ? "true" : "false");
            TRACE("Loaded configuration from: %s\n", config_paths[i]);
            break;
        }
    }
    
    if (!config_loaded) {
        TRACE("No configuration file found, using registry/defaults\n");
    }

    /* Look for environment variables to override registry config values */

    if (GetEnvironmentVariableA("PWASIO_NUMBER_INPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_inputs = result;
    }

    if (GetEnvironmentVariableA("PWASIO_NUMBER_OUTPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_outputs = result;
    }

    if (GetEnvironmentVariableA("PWASIO_NUMBER_MIDI_INPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_midi_inputs = result;
    }

    if (GetEnvironmentVariableA("PWASIO_NUMBER_MIDI_OUTPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE)
            This->wineasio_number_midi_outputs = result;
    }

    if (GetEnvironmentVariableA("PWASIO_CONNECT_TO_HARDWARE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        parse_boolean_env(environment_variable, &This->wineasio_connect_to_hardware);
    }

    if (GetEnvironmentVariableA("PWASIO_BUFFERSIZE_IS_FIXED", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        parse_boolean_env(environment_variable, &This->wineasio_fixed_buffersize);
    }

    if (GetEnvironmentVariableA("PWASIO_PREFERRED_BUFFERSIZE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        errno = 0;
        result = strtol(environment_variable, 0, 10);
        if (errno != ERANGE) {
            This->wineasio_preferred_buffersize = result;
            /* Update current buffer size to match environment variable override */
            This->asio_current_buffersize = result;
        }
    }

    /* if wineasio_preferred_buffersize is out of range, then set to ASIO_PREFERRED_BUFFERSIZE */
    if (!(This->wineasio_preferred_buffersize >= ASIO_MINIMUM_BUFFERSIZE
            && This->wineasio_preferred_buffersize <= ASIO_MAXIMUM_BUFFERSIZE)) {
        This->wineasio_preferred_buffersize = ASIO_PREFERRED_BUFFERSIZE;
        /* Update current buffer size to match corrected preferred size */
        This->asio_current_buffersize = ASIO_PREFERRED_BUFFERSIZE;
    }

    This->wineasio_number_inputs = sanitize_channel_count(
        "input", This->wineasio_number_inputs, PW_ASIO_DEFAULT_INPUT_CHANNELS);
    This->wineasio_number_outputs = sanitize_channel_count(
        "output", This->wineasio_number_outputs, PW_ASIO_DEFAULT_OUTPUT_CHANNELS);
    This->wineasio_number_midi_inputs = sanitize_midi_port_count(
        "input", This->wineasio_number_midi_inputs, PW_ASIO_DEFAULT_MIDI_INPUTS);
    This->wineasio_number_midi_outputs = sanitize_midi_port_count(
        "output", This->wineasio_number_midi_outputs, PW_ASIO_DEFAULT_MIDI_OUTPUTS);



    return;
}
