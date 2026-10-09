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
#include <math.h>
#include <pipewire/loop.h>
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

#define MAX_ENVIRONMENT_SIZE        32
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
    
    /* Wine-compatible buffer management */
    void                        *wine_buffers[2];  /* Wine-allocated buffers */
    size_t                       buffer_size;      /* Size of each buffer in bytes */
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
    _Atomic INT                 asio_driver_state;
    _Atomic uint64_t            asio_sample_position;
    double                      asio_sample_rate;
    ASIOTime                    asio_time;
    _Atomic uint64_t            asio_time_stamp;
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
static  void    Uninit(IWineASIOImpl *This);
static  void    free_asio_buffers(IWineASIOImpl *This);

HIDDEN void GuiApplyConfig(struct pwasio_gui_conf *conf);
HIDDEN void GuiLoadConfig(struct pwasio_gui_conf *conf);

static DWORD WINAPI jack_thread_creator_helper(LPVOID arg);
static int          jack_thread_creator(pthread_t* thread_id, const pthread_attr_t* attr, void *(*function)(void*), void* arg);

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

/* Each thread creation has independent startup data. PipeWire's data loop
 * runs in Wine context, so host callbacks can execute there directly. */
struct wine_thread_start {
    void *(*function)(void *);
    void *arg;
    pthread_t thread_id;
    HANDLE ready;
};

static char g_last_error[256] = "No error";

static void set_last_error(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
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

static void process_midi_input(IWineASIOImpl *This, uint32_t frame_count) {
    if (!This->midi_input_ports) return;
    for (int idx = 0; idx < This->wineasio_number_midi_inputs; ++idx) {
        MidiPort *port = &This->midi_input_ports[idx];
        if (!port->active || !port->port) continue;
        struct pw_buffer *buffer = pw_filter_dequeue_buffer(port->port);
        if (!buffer) continue;
        struct spa_buffer *spa = buffer->buffer;
        if (spa && spa->n_datas > 0) {
            struct spa_data *d = &spa->datas[0];
            if (d->data && d->chunk && d->chunk->offset <= d->maxsize &&
                d->chunk->size <= d->maxsize - d->chunk->offset &&
                d->chunk->size >= sizeof(struct spa_pod_sequence)) {
                struct spa_pod *pod = (struct spa_pod *)((char *)d->data + d->chunk->offset);
                if (spa_pod_is_sequence(pod) && pod->size <= d->chunk->size - sizeof(*pod)) {
                    struct spa_pod_control *ctrl;
                    SPA_POD_SEQUENCE_FOREACH((struct spa_pod_sequence *)pod, ctrl) {
                        uint32_t size = ctrl->value.size;
                        if (ctrl->type != SPA_CONTROL_Midi || ctrl->value.type != SPA_TYPE_Bytes ||
                            size == 0 || size > PW_ASIO_MIDI_MAX_DATA) continue;
                        uint32_t offset = ctrl->offset;
                        if (frame_count && offset >= frame_count) offset = frame_count - 1;
                        midi_buffer_try_write(&This->midi_in_buffer, idx, offset,
                                              SPA_POD_BODY(&ctrl->value), (uint8_t)size);
                    }
                }
            }
        }
        pw_filter_queue_buffer(port->port, buffer);
    }
}

static void process_midi_output(IWineASIOImpl *This, uint32_t frame_count) {
    if (!This->midi_output_ports) return;
    for (int idx = 0; idx < This->wineasio_number_midi_outputs; ++idx) {
        MidiPort *port = &This->midi_output_ports[idx];
        if (!port->active || !port->port) continue;
        struct pw_buffer *buffer = pw_filter_dequeue_buffer(port->port);
        if (!buffer) continue;
        struct spa_buffer *spa = buffer->buffer;
        if (spa && spa->n_datas > 0) {
            struct spa_data *d = &spa->datas[0];
            if (d->data && d->chunk) {
                d->chunk->offset = 0;
                d->chunk->size = 0;
                d->chunk->stride = 1;
                d->chunk->flags = 0;
                if (d->maxsize >= sizeof(struct spa_pod_sequence)) {
                    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(d->data, d->maxsize);
                    struct spa_pod_frame frame;
                    spa_pod_builder_push_sequence(&builder, &frame, 0);
                    struct midi_event ev;
                    while (midi_buffer_try_read(&This->midi_out_buffer, &ev)) {
                        if (ev.port != idx || ev.size == 0 || ev.size > PW_ASIO_MIDI_MAX_DATA) continue;
                        uint32_t needed = sizeof(struct spa_pod_control) + SPA_ROUND_UP_N(ev.size, SPA_POD_ALIGN);
                        /* Check capacity before adding a control, so an overflow
                         * cannot leave a partial control in the sequence. */
                        if (needed > d->maxsize - builder.state.offset) break;
                        uint32_t offset = ev.offset;
                        if (frame_count && offset >= frame_count) offset = frame_count - 1;
                        spa_pod_builder_control(&builder, offset, SPA_CONTROL_Midi);
                        spa_pod_builder_bytes(&builder, ev.data, ev.size);
                    }
                    spa_pod_builder_pop(&builder, &frame);
                    d->chunk->size = builder.state.offset;
                }
            }
        }
        pw_filter_queue_buffer(port->port, buffer);
    }
}

/* Like pw_filter_get_dsp_buffer, but also checks the negotiated capacity.
 * External graph changes must never make a copy/zero-fill overrun a port. */
static void *audio_dsp_buffer(IOChannel *channel, uint32_t samples, bool output) {
    if (!channel->port) return NULL;
    struct pw_buffer *buffer = pw_filter_dequeue_buffer(channel->port);
    if (!buffer) return NULL;
    void *pointer = NULL;
    struct spa_buffer *spa = buffer->buffer;
    if (spa && spa->n_datas > 0) {
        struct spa_data *d = &spa->datas[0];
        uint64_t bytes = (uint64_t)samples * sizeof(float);
        if (d->data && d->chunk) {
            if (output) {
                d->chunk->offset = 0;
                d->chunk->size = bytes <= d->maxsize ? bytes : 0;
                d->chunk->stride = sizeof(float);
                d->chunk->flags = 0;
                if (bytes <= d->maxsize) pointer = d->data;
            } else if (d->chunk->offset <= d->maxsize &&
                       bytes <= d->maxsize - d->chunk->offset && bytes <= d->chunk->size) {
                pointer = (char *)d->data + d->chunk->offset;
            }
        }
    }
    pw_filter_queue_buffer(channel->port, buffer);
    return pointer;
}

static void silence_outputs(IWineASIOImpl *This, uint32_t samples) {
    if (!This->output_channel) return;
    for (int idx = 0; idx < This->wineasio_number_outputs; ++idx) {
        IOChannel *chan = &This->output_channel[idx];
        if (chan->port) {
            void *buffer = audio_dsp_buffer(chan, samples, true);
            if (buffer) memset(buffer, 0, samples * sizeof(float));
        }
    }
}

static void pipewire_process_callback(void *data, struct spa_io_position *position) {
    IWineASIOImpl *This = data;
    if (!This || !position) return;
    uint32_t samples = position->clock.duration;

    /* ASIO buffer sizes are fixed until the host disposes them. Do not feed
     * partial/doubled graph cycles to the host: that changes pitch and tempo.
     * The node requests matching quantum/rate on connection. Silence any
     * transitional or externally forced mismatch, without logging in RT. */
    if (unlikely(This->asio_driver_state != Running || !This->asio_callbacks ||
                 samples != This->asio_current_buffersize ||
                 position->clock.rate.num != 1 ||
                 position->clock.rate.denom != (uint32_t)This->asio_sample_rate)) {
        silence_outputs(This, samples);
        return;
    }

    size_t bytes = samples * sizeof(float);
    LONG index = This->asio_buffer_index;
    for (int idx = 0; idx < This->wineasio_number_inputs; ++idx) {
        IOChannel *chan = &This->input_channel[idx];
        if (!chan->active) continue;
        void *buffer = audio_dsp_buffer(chan, samples, false);
        if (buffer) memcpy(chan->wine_buffers[index], buffer, bytes);
        else memset(chan->wine_buffers[index], 0, bytes);
    }

    process_midi_input(This, samples);
    uint64_t sample_position = This->asio_sample_position;
    uint64_t timestamp = position->clock.nsec;
    if (position->clock.flags & SPA_IO_CLOCK_FLAG_FREEWHEEL)
        timestamp = (uint64_t)((double)sample_position * SPA_NSEC_PER_SEC / This->asio_sample_rate);
    This->asio_time_stamp = timestamp;

    /* PipeWire created this thread using jack_thread_creator (CreateThread).
     * Synchronous callbacks avoid a second scheduler handoff and guarantee
     * the host has finished writing before we copy/advance/free its buffers. */
    if (This->asio_time_info_mode) {
        This->asio_time.timeInfo.samplePosition = ASIO_LONG(ASIOSamples, sample_position);
        This->asio_time.timeInfo.systemTime = ASIO_LONG(ASIOTimeStamp, timestamp);
        This->asio_time.timeInfo.sampleRate = This->asio_sample_rate;
        This->asio_time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        This->asio_callbacks->bufferSwitchTimeInfo(&This->asio_time, index, ASIOTrue);
    } else {
        This->asio_callbacks->bufferSwitch(index, ASIOTrue);
    }

    for (int idx = 0; idx < This->wineasio_number_outputs; ++idx) {
        IOChannel *chan = &This->output_channel[idx];
        void *buffer = audio_dsp_buffer(chan, samples, true);
        if (!buffer) continue;
        if (chan->active) memcpy(buffer, chan->wine_buffers[index], bytes);
        else memset(buffer, 0, bytes);
    }
    process_midi_output(This, samples);
    This->asio_sample_position = sample_position + samples;
    This->asio_time_stamp = timestamp + (samples * SPA_NSEC_PER_SEC) / (uint64_t)This->asio_sample_rate;
    This->asio_buffer_index ^= 1;
}

static struct pw_filter_events const pw_filter_events = {
    .version = PW_VERSION_FILTER_EVENTS,
    .state_changed = pipewire_state_changed_callback,
    .io_changed = pipewire_io_changed_callback,
    .param_changed = pipewire_param_changed_callback,
    .process = pipewire_process_callback,
};

/*****************************************************************************
 * WineASIO instance creation
 */

HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj, IUnknown *cls_factory)
{
    IWineASIOImpl   *pobj;

    /* TRACE("riid: %s, ppobj: %p\n", debugstr_guid(riid), ppobj); */

    if (!ppobj) return E_POINTER;
    *ppobj = NULL;
    if (!IsEqualIID(riid, &CLSID_PipeWine) && !IsEqualIID(riid, &IID_IUnknown))
        return E_NOINTERFACE;

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

    *ppvObject = NULL;
    if (IsEqualIID(&CLSID_PipeWine, riid) || IsEqualIID(&IID_IUnknown, riid))
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

    if (ref != 0) return ref;

    if (This->asio_driver_state == Running) Stop(iface);
    if (This->asio_driver_state == Prepared) DisposeBuffers(iface);
    if (This->gui) {
        pwasio_destroy_gui(This->gui);
        This->gui = NULL;
    }
    Uninit(This);
    if (This->input_channel) free_asio_buffers(This);
    if (This->input_channel) HeapFree(GetProcessHeap(), 0, This->input_channel);
    if (This->midi_input_ports) HeapFree(GetProcessHeap(), 0, This->midi_input_ports);
    if (This->midi_buffers_initialized) {
        midi_buffer_destroy(&This->midi_in_buffer);
        midi_buffer_destroy(&This->midi_out_buffer);
    }
    This->cls_factory->lpVtbl->Release(This->cls_factory);
    HeapFree(GetProcessHeap(), 0, This);
    return 0;
}

static void Uninit(IWineASIOImpl *This) {
    if (!This) return;

    /* Cleanup PipeWire filter if exists */
    if (This->pw_filter) {
        user_pw_lock_loop(This->pw_helper);
        user_pw_configure_audio_connections(This->pw_helper, SPA_ID_INVALID, NULL, NULL, 0, 0);
        pw_filter_disconnect(This->pw_filter);
        pw_filter_destroy(This->pw_filter);
        user_pw_unlock_loop(This->pw_helper);
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
}

/* Helper function to clear all audio buffers for clean state transitions */
static void clear_audio_buffers(IWineASIOImpl *This, const char *context) {
    
    TRACE("Clearing audio buffers (%s)\n", context);
    
    /* Clear input channel buffers */
    for (int i = 0; i < This->wineasio_number_inputs; i++) {
        if (This->input_channel[i].active) {
            /* Clear Wine-allocated buffers with bounds checking */
            if (This->input_channel[i].wine_buffers[0] && This->input_channel[i].buffer_size > 0) {
                memset(This->input_channel[i].wine_buffers[0], 0, This->input_channel[i].buffer_size);
            }
            if (This->input_channel[i].wine_buffers[1] && This->input_channel[i].buffer_size > 0) {
                memset(This->input_channel[i].wine_buffers[1], 0, This->input_channel[i].buffer_size);
            }
        }
    }
    
    /* Clear output channel buffers */
    for (int i = 0; i < This->wineasio_number_outputs; i++) {
        if (This->output_channel[i].active) {
            /* Clear Wine-allocated buffers with bounds checking */
            if (This->output_channel[i].wine_buffers[0] && This->output_channel[i].buffer_size > 0) {
                memset(This->output_channel[i].wine_buffers[0], 0, This->output_channel[i].buffer_size);
            }
            if (This->output_channel[i].wine_buffers[1] && This->output_channel[i].buffer_size > 0) {
                memset(This->output_channel[i].wine_buffers[1], 0, This->output_channel[i].buffer_size);
            }
        }
    }
    
}

static ASIOError InitPorts(IWineASIOImpl *This) {
    int idx;
    char pod_buffer[0x1000];
    struct spa_pod_builder pod_builder;

    printf("PipeWine ASIO: InitPorts() starting\n");
    /* Allocate IOChannel structures */
    This->input_channel = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (This->wineasio_number_inputs + This->wineasio_number_outputs) * sizeof(IOChannel));
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
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)),
            SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_STEP_Int(
                This->asio_current_buffersize * sizeof(float),
                ASIO_MINIMUM_BUFFERSIZE * sizeof(float),
                ASIO_MAXIMUM_BUFFERSIZE * sizeof(float),
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
        This->midi_input_ports = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, midi_total * sizeof(MidiPort));
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
                SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)),
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

    for (idx = 0; idx < This->wineasio_number_inputs + This->wineasio_number_outputs; ++idx)
        if (!This->input_channel[idx].port) return ASE_NoMemory;
    for (idx = 0; idx < This->wineasio_number_midi_inputs + This->wineasio_number_midi_outputs; ++idx)
        if (!This->midi_input_ports[idx].port) return ASE_NoMemory;
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

    if (This->asio_driver_state != Loaded) return ASIOTrue;
    This->sys_ref = sysRef;
    configure_driver(This);

    if (!(This->pw_helper = user_pw_create_helper(0, NULL, &init_args)))
    {
        set_last_error("Failed to initialize PipeWire helper");
        return ASIOFalse;
    }

    printf("PipeWine ASIO: Setting up GUI config\n");
    This->gui = NULL;
    This->gui_conf.user = This;
    This->gui_conf.closed = NULL;
    This->gui_conf.apply_config = GuiApplyConfig;
    This->gui_conf.load_config = GuiLoadConfig;
    This->gui_conf.pw_helper = This->pw_helper;
    This->gui_conf.cf_buffer_size = 1024;

    printf("PipeWine ASIO: Locking PipeWire loop\n");
    user_pw_lock_loop(This->pw_helper);

    This->pw_filter = pw_filter_new(This->pw_core, This->client_name, pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_MEDIA_CLASS, "Stream/Audio",
        PW_KEY_NODE_AUTOCONNECT, "false",
        NULL
    ));

    if (!This->pw_filter) {
        user_pw_unlock_loop(This->pw_helper);
        ERR("Failed to create filter node\n");
        set_last_error("Failed to create PipeWire filter node");
        Uninit(This);
        return ASIOFalse;
    }

    pw_filter_add_listener(This->pw_filter, &This->pw_filter_listener, &pw_filter_events, This);

    if (InitPorts(This) != ASE_OK) {
        user_pw_unlock_loop(This->pw_helper);
        set_last_error("Failed to initialize PipeWire ports");
        Uninit(This);
        if (This->input_channel) HeapFree(GetProcessHeap(), 0, This->input_channel);
        if (This->midi_input_ports) HeapFree(GetProcessHeap(), 0, This->midi_input_ports);
        This->input_channel = This->output_channel = NULL;
        This->midi_input_ports = This->midi_output_ports = NULL;
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
            This->asio_time.timeCode.flags = 0;
        }
    }

    /* Set driver state to Running - this enables the process callback */
    This->asio_driver_state = Running;
    user_pw_lock_loop(This->pw_helper);
    int result = pw_filter_set_active(This->pw_filter, true);
    user_pw_unlock_loop(This->pw_helper);
    if (result < 0) {
        This->asio_driver_state = Prepared;
        set_last_error("Failed to activate PipeWire filter");
        return ASE_HWMalfunction;
    }
    TRACE("PipeWine successfully started with clean buffers\n");
    return ASE_OK;
}

/*
 * ASIOError Stop(void);
 *  Function:   Stop PipeWire IO processing
 *  Returns:    ASE_NotPresent on missing IO
 *  Note:       BufferSwitch() must not called after returning
 */

static int process_fence(struct spa_loop *loop, bool async, uint32_t seq,
                         const void *data, size_t size, void *userdata) {
    return 0;
}

DEFINE_THISCALL_WRAPPER(Stop,4)
HIDDEN ASIOError STDMETHODCALLTYPE Stop(LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl*)iface;

    TRACE("iface: %p\n", iface);

    if (This->asio_driver_state != Running) {
        set_last_error("Driver is not running");
        return ASE_NotPresent;
    }

    This->asio_driver_state = Prepared;
    user_pw_lock_loop(This->pw_helper);
    int result = pw_filter_set_active(This->pw_filter, false);
    struct pw_loop *data_loop = pw_filter_get_data_loop(This->pw_filter);
    /* pw_loop_invoke with a blocking no-op serializes with the RT callback. */
    if (data_loop) pw_loop_invoke(data_loop, process_fence, 0, NULL, 0, true, NULL);
    user_pw_unlock_loop(This->pw_helper);
    if (result < 0) {
        set_last_error("Failed to deactivate PipeWire filter");
        return ASE_HWMalfunction;
    }

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
    *granularity = -1;
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

    if (!isfinite(sampleRate) || sampleRate < 0 || sampleRate > UINT32_MAX ||
        sampleRate != (uint32_t)sampleRate ||
        !pw_asio_is_valid_sample_rate((uint32_t)sampleRate)) return ASE_NoClock;
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

    ASIOError result = CanSampleRate(iface, sampleRate);
    if (result != ASE_OK) return result;
    if (This->asio_sample_rate == sampleRate) return ASE_OK;
    /* A host must dispose its existing stream before changing its clock. */
    if (This->asio_driver_state >= Prepared) return ASE_InvalidMode;
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

    if (!clocks || !numSources || *numSources < 1)
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

    if (!info) return ASE_InvalidParameter;
    if (This->asio_driver_state == Loaded) return ASE_NotPresent;
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

/* Release only ASIO-owned memory; PipeWire owns its DSP buffers and ports. */
static void free_asio_buffers(IWineASIOImpl *This) {
    for (int i = 0; i < This->wineasio_number_inputs + This->wineasio_number_outputs; ++i) {
        IOChannel *chan = &This->input_channel[i];
        for (int index = 0; index < 2; ++index) {
            if (chan->wine_buffers[index]) HeapFree(GetProcessHeap(), 0, chan->wine_buffers[index]);
            chan->wine_buffers[index] = NULL;
        }
        chan->buffer_size = 0;
        chan->active = false;
    }
    This->asio_callbacks = NULL;
    This->asio_active_inputs = This->asio_active_outputs = 0;
}

DEFINE_THISCALL_WRAPPER(CreateBuffers,20)
HIDDEN ASIOError STDMETHODCALLTYPE CreateBuffers(LPWINEASIO iface, ASIOBufferInfo *bufferInfo,
                                                LONG numChannels, LONG bufferSize, ASIOCallbacks *callbacks)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    ASIOError result = ASE_HWMalfunction;
    bool selected[2][PW_ASIO_MAX_CHANNELS] = {{false}};
    if (This->asio_driver_state != Initialized) return ASE_NotPresent;
    if (!bufferInfo || !callbacks || !callbacks->asioMessage ||
        numChannels <= 0 || numChannels > This->wineasio_number_inputs + This->wineasio_number_outputs)
        return ASE_InvalidParameter;
    if (!pw_asio_is_valid_buffer_size(bufferSize) ||
        (This->wineasio_fixed_buffersize && bufferSize != This->asio_current_buffersize))
        return ASE_InvalidMode;

    /* Validate the whole request before indexing or changing any channel. */
    for (int i = 0; i < numChannels; ++i) {
        int direction = bufferInfo[i].isInput ? 0 : 1;
        LONG channel = bufferInfo[i].channelNum;
        int limit = direction == 0 ? This->wineasio_number_inputs : This->wineasio_number_outputs;
        if (channel < 0 || channel >= limit || selected[direction][channel])
            return ASE_InvalidParameter;
        selected[direction][channel] = true;
    }
    bool time_info = callbacks->bufferSwitchTimeInfo &&
                     callbacks->asioMessage(kAsioSupportsTimeInfo, 0, NULL, NULL);
    if (!time_info && !callbacks->bufferSwitch) return ASE_InvalidParameter;

    This->asio_current_buffersize = bufferSize;
    This->asio_time_info_mode = time_info;
    This->asio_can_time_code = false;
    This->asio_callbacks = callbacks;
    for (int i = 0; i < numChannels; ++i) {
        IOChannel *chan = bufferInfo[i].isInput ? &This->input_channel[bufferInfo[i].channelNum] :
                                                &This->output_channel[bufferInfo[i].channelNum];
        bufferInfo[i].buffers[0] = bufferInfo[i].buffers[1] = NULL;
        chan->buffer_size = bufferSize * sizeof(float);
        chan->wine_buffers[0] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, chan->buffer_size);
        chan->wine_buffers[1] = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, chan->buffer_size);
        if (!chan->wine_buffers[0] || !chan->wine_buffers[1]) {
            result = ASE_NoMemory;
            goto fail;
        }
        chan->active = true;
        if (bufferInfo[i].isInput) ++This->asio_active_inputs;
        else ++This->asio_active_outputs;
    }

    /* rate is 1/sampleRate; latency is frames/sampleRate. Quantum/rate
     * constraints apply only while this node is active (ASIOStart/Stop). */
    char rate[32], latency[32], quantum[32], force_rate[32];
    snprintf(rate, sizeof(rate), "1/%u", (uint32_t)This->asio_sample_rate);
    snprintf(latency, sizeof(latency), "%d/%u", bufferSize, (uint32_t)This->asio_sample_rate);
    snprintf(quantum, sizeof(quantum), "%d", bufferSize);
    snprintf(force_rate, sizeof(force_rate), "%u", (uint32_t)This->asio_sample_rate);
    const struct spa_dict_item items[] = {
        { PW_KEY_NODE_RATE, rate },
        { PW_KEY_NODE_LATENCY, latency },
        { PW_KEY_NODE_FORCE_QUANTUM, quantum },
        { PW_KEY_NODE_FORCE_RATE, force_rate },
    };
    const struct spa_dict dict = SPA_DICT_INIT(items, ARRAYSIZE(items));
    char pod_buffer[256];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
    const struct spa_pod *params[] = {
        spa_process_latency_build(&builder, SPA_PARAM_ProcessLatency,
            &SPA_PROCESS_LATENCY_INFO_INIT(.rate = bufferSize)),
    };
    user_pw_lock_loop(This->pw_helper);
    int res = pw_filter_update_properties(This->pw_filter, NULL, &dict);
    if (res >= 0)
        res = pw_filter_connect(This->pw_filter, PW_FILTER_FLAG_RT_PROCESS | PW_FILTER_FLAG_INACTIVE,
                                params, ARRAYSIZE(params));
    user_pw_unlock_loop(This->pw_helper);
    if (res < 0 || !user_pw_wait_for_filter_state(This->pw_helper, This->pw_filter,
                                                PW_FILTER_STATE_PAUSED, 5000)) goto fail;

    if (This->wineasio_connect_to_hardware) {
        char source[DEVICE_NAME_SIZE * 4] = {0}, sink[DEVICE_NAME_SIZE * 4] = {0};
        uint64_t inputs = 0, outputs = 0;
        WideCharToMultiByte(CP_UTF8, 0, This->pwasio_input_device_name, -1, source, sizeof(source), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, This->pwasio_output_device_name, -1, sink, sizeof(sink), NULL, NULL);
        for (int i = 0; i < numChannels; ++i) {
            if (bufferInfo[i].isInput) inputs |= UINT64_C(1) << bufferInfo[i].channelNum;
            else outputs |= UINT64_C(1) << bufferInfo[i].channelNum;
        }
        user_pw_lock_loop(This->pw_helper);
        uint32_t node = pw_filter_get_node_id(This->pw_filter);
        user_pw_configure_audio_connections(This->pw_helper, node, source, sink, inputs, outputs);
        user_pw_unlock_loop(This->pw_helper);
    }

    for (int i = 0; i < numChannels; ++i) {
        IOChannel *chan = bufferInfo[i].isInput ? &This->input_channel[bufferInfo[i].channelNum] :
                                                &This->output_channel[bufferInfo[i].channelNum];
        bufferInfo[i].buffers[0] = chan->wine_buffers[0];
        bufferInfo[i].buffers[1] = chan->wine_buffers[1];
    }
    This->asio_driver_state = Prepared;
    return ASE_OK;

fail:
    user_pw_lock_loop(This->pw_helper);
    if (pw_filter_get_state(This->pw_filter, NULL) != PW_FILTER_STATE_UNCONNECTED)
        pw_filter_disconnect(This->pw_filter);
    user_pw_unlock_loop(This->pw_helper);
    free_asio_buffers(This);
    for (int i = 0; i < numChannels; ++i)
        bufferInfo[i].buffers[0] = bufferInfo[i].buffers[1] = NULL;
    set_last_error("Failed to prepare audio buffers");
    return result;
}

DEFINE_THISCALL_WRAPPER(DisposeBuffers,4)
HIDDEN ASIOError STDMETHODCALLTYPE DisposeBuffers(LPWINEASIO iface)
{
    IWineASIOImpl *This = (IWineASIOImpl *)iface;
    if (This->asio_driver_state == Running) {
        ASIOError result = Stop(iface);
        if (result != ASE_OK) return result;
    }
    if (This->asio_driver_state != Prepared) return ASE_NotPresent;

    /* Disconnecting joins outstanding processing. Keep the filter and ports
     * so CreateBuffers can connect them again without a second ASIOInit. */
    user_pw_lock_loop(This->pw_helper);
    user_pw_configure_audio_connections(This->pw_helper, SPA_ID_INVALID, NULL, NULL, 0, 0);
    int res = pw_filter_disconnect(This->pw_filter);
    user_pw_unlock_loop(This->pw_helper);
    if (res < 0) return ASE_HWMalfunction;
    free_asio_buffers(This);
    if (This->midi_buffers_initialized) {
        midi_buffer_clear(&This->midi_in_buffer);
        midi_buffer_clear(&This->midi_out_buffer);
    }
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
        if (!This->gui) return ASE_NotPresent;
        pwasio_destroy_gui(This->gui);
        This->gui = NULL;
    }
    return ASE_OK;
}

HIDDEN void GuiApplyConfig(struct pwasio_gui_conf *conf)
{
    IWineASIOImpl *This = conf->user;
    if (!pw_asio_is_valid_buffer_size(conf->cf_buffer_size) ||
        !pw_asio_is_valid_sample_rate(conf->cf_sample_rate) ||
        conf->cf_input_channels < 1 || conf->cf_input_channels > PW_ASIO_MAX_CHANNELS ||
        conf->cf_output_channels < 1 || conf->cf_output_channels > PW_ASIO_MAX_CHANNELS)
        return;
    This->wineasio_preferred_buffersize = conf->cf_buffer_size;
    /* Channel/device changes require reopening the driver. The saved file
     * supplies them to the next Init; current port arrays retain their size. */
    if (This->asio_driver_state < Prepared) {
        This->asio_current_buffersize = conf->cf_buffer_size;
        This->asio_sample_rate = conf->cf_sample_rate;
    } else if (This->asio_callbacks && This->asio_callbacks->asioMessage) {
        This->asio_callbacks->asioMessage(kAsioResetRequest, 0, NULL, NULL);
    }
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
            if (buf->count > buf->capacity || (!buf->events && buf->count)) {
                buf->count = 0;
                return ASE_InvalidParameter;
            }
            if (!buf->count) return ASE_SUCCESS;

            uint32_t accepted = 0;
            for (uint32_t i = 0; i < buf->count; ++i) {
                PWASIO_MidiEvent *ev = &buf->events[i];
                if (ev->port >= (uint8_t)This->wineasio_number_midi_outputs ||
                    ev->size == 0 || ev->size > PW_ASIO_MIDI_MAX_DATA) {
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
            return ASE_NotPresent;
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
            return ASE_NotPresent;
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

/* PipeWire thread creator: Wine context with per-request startup storage. */
static int jack_thread_creator(pthread_t *thread_id, const pthread_attr_t *attr,
                               void *(*function)(void *), void *arg)
{
    struct wine_thread_start start = { .function = function, .arg = arg };
    start.ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!start.ready) return EAGAIN;
    HANDLE thread = CreateThread(NULL, 0, jack_thread_creator_helper, &start, 0, NULL);
    if (!thread) {
        CloseHandle(start.ready);
        return EAGAIN;
    }
    WaitForSingleObject(start.ready, INFINITE);
    *thread_id = start.thread_id;
    CloseHandle(start.ready);
    CloseHandle(thread);
    return 0;
}

static DWORD WINAPI jack_thread_creator_helper(LPVOID arg)
{
    struct wine_thread_start *start = arg;
    void *(*function)(void *) = start->function;
    void *function_arg = start->arg;
    start->thread_id = pthread_self();
    SetEvent(start->ready);
    function(function_arg);
    return 0;
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

static bool read_environment(const char *name, char *value, DWORD capacity) {
    DWORD length = GetEnvironmentVariableA(name, value, capacity);
    return length > 0 && length < capacity;
}

static int environment_int(const char *value, int fallback) {
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' && parsed >= INT_MIN && parsed <= INT_MAX ?
           (int)parsed : fallback;
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
    WCHAR application_path[MAX_PATH] = {0};
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
    size = sizeof(This->pwasio_input_device_name);
    status = RegQueryValueExW(hkey, value_pwasio_input_device, NULL, &type, (LPBYTE) &This->pwasio_input_device_name, &size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
    {
        if (type == REG_SZ) {
            This->pwasio_input_device_name[DEVICE_NAME_SIZE - 1] = 0;
        }
    }
    else
    {
        size = 0;
        result = RegSetValueExW(hkey, value_pwasio_input_device, 0, REG_SZ, (LPBYTE) &This->pwasio_input_device_name, size);
    }

    /* output device name */
    This->pwasio_output_device_name[0] = 0;
    size = sizeof(This->pwasio_output_device_name);
    status = RegQueryValueExW(hkey, value_pwasio_output_device, NULL, &type, (LPBYTE) &This->pwasio_output_device_name, &size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
    {
        if (type == REG_SZ) {
            This->pwasio_output_device_name[DEVICE_NAME_SIZE - 1] = 0;
        }
    }
    else
    {
        size = 0;
        result = RegSetValueExW(hkey, value_pwasio_output_device, 0, REG_SZ, (LPBYTE) &This->pwasio_output_device_name, size);
    }

    /* override the PipeWire client name gotten from the application name */
    size = GetEnvironmentVariableW(u"PWASIO_CLIENT_NAME", application_path, ASIO_MAX_NAME_LENGTH);
    if (size == 0 || size >= ASIO_MAX_NAME_LENGTH) {
        /* get client name by stripping path and extension */
        GetModuleFileNameW(0, application_path, MAX_PATH);
        application_name = strrchrW(application_path, L'.');
        if (application_name) *application_name = 0;
        application_name = strrchrW(application_path, L'\\');
        application_name = application_name ? application_name + 1 : application_path;
    } else {
        application_name = application_path;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, application_name, -1, This->client_name, ASIO_MAX_NAME_LENGTH, NULL, NULL))
        strcpy(This->client_name, "PipeWine");

    RegCloseKey(hkey);

    /* Load configuration from PipeWire ASIO config file (overrides registry, but can be overridden by environment) */
    struct pw_helper_init_args config_args;
    pw_asio_init_default_config(&config_args);
    config_args.buffer_size = This->wineasio_preferred_buffersize;
    config_args.sample_rate = This->asio_sample_rate;
    config_args.num_input_channels = This->wineasio_number_inputs;
    config_args.num_output_channels = This->wineasio_number_outputs;
    config_args.auto_connect = This->wineasio_connect_to_hardware;
    char *config_path = pw_asio_get_config_path();
    if (config_path) {
        struct pw_helper_init_args file_args;
        if (pw_asio_load_config(&file_args, config_path) == 0) config_args = file_args;
        free(config_path);
    }
    pw_asio_apply_env_overrides(&config_args);
    This->wineasio_preferred_buffersize = config_args.buffer_size;
    This->asio_current_buffersize = config_args.buffer_size;
    This->asio_sample_rate = pw_asio_is_valid_sample_rate(config_args.sample_rate) ?
                             config_args.sample_rate : PW_ASIO_DEFAULT_SAMPLE_RATE;
    This->wineasio_number_inputs = config_args.num_input_channels;
    This->wineasio_number_outputs = config_args.num_output_channels;
    This->wineasio_connect_to_hardware = config_args.auto_connect;
    if (config_args.input_device_name)
        MultiByteToWideChar(CP_UTF8, 0, config_args.input_device_name, -1,
                            This->pwasio_input_device_name, DEVICE_NAME_SIZE);
    if (config_args.output_device_name)
        MultiByteToWideChar(CP_UTF8, 0, config_args.output_device_name, -1,
                            This->pwasio_output_device_name, DEVICE_NAME_SIZE);
    if (config_args.client_name && *config_args.client_name)
        snprintf(This->client_name, sizeof(This->client_name), "%s", config_args.client_name);

    /* Look for environment variables to override registry config values */

    if (read_environment("PWASIO_NUMBER_INPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
        This->wineasio_number_inputs = environment_int(environment_variable, This->wineasio_number_inputs);

    if (read_environment("PWASIO_NUMBER_OUTPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
        This->wineasio_number_outputs = environment_int(environment_variable, This->wineasio_number_outputs);

    if (read_environment("PWASIO_NUMBER_MIDI_INPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
        This->wineasio_number_midi_inputs = environment_int(environment_variable, This->wineasio_number_midi_inputs);

    if (read_environment("PWASIO_NUMBER_MIDI_OUTPUTS", environment_variable, MAX_ENVIRONMENT_SIZE))
        This->wineasio_number_midi_outputs = environment_int(environment_variable, This->wineasio_number_midi_outputs);

    if (read_environment("PWASIO_CONNECT_TO_HARDWARE", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        parse_boolean_env(environment_variable, &This->wineasio_connect_to_hardware);
    }

    if (read_environment("PWASIO_BUFFERSIZE_IS_FIXED", environment_variable, MAX_ENVIRONMENT_SIZE))
    {
        parse_boolean_env(environment_variable, &This->wineasio_fixed_buffersize);
    }

    if (read_environment("PWASIO_PREFERRED_BUFFERSIZE", environment_variable, MAX_ENVIRONMENT_SIZE)) {
        This->wineasio_preferred_buffersize = environment_int(environment_variable, This->wineasio_preferred_buffersize);
        This->asio_current_buffersize = This->wineasio_preferred_buffersize;
    }

    /* if wineasio_preferred_buffersize is out of range, then set to ASIO_PREFERRED_BUFFERSIZE */
    if (!pw_asio_is_valid_buffer_size(This->wineasio_preferred_buffersize)) {
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
