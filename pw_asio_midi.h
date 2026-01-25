#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PW_ASIO_MIDI_API_VERSION 1
#define PW_ASIO_MIDI_MAX_EVENTS 1024
#define PW_ASIO_MIDI_MAX_DATA 4

/* Custom ASIOFuture selectors for PipeWine MIDI */
#define PW_ASIO_MIDI_SELECTOR_BASE 0x50574D00u /* 'PWM\0' */
#define PW_ASIO_MIDI_SELECTOR_GET_INFO   (PW_ASIO_MIDI_SELECTOR_BASE + 1)
#define PW_ASIO_MIDI_SELECTOR_GET_INPUT  (PW_ASIO_MIDI_SELECTOR_BASE + 2)
#define PW_ASIO_MIDI_SELECTOR_PUT_OUTPUT (PW_ASIO_MIDI_SELECTOR_BASE + 3)

typedef struct PWASIO_MidiEvent {
    uint32_t offset; /* Sample offset within the current buffer */
    uint8_t size;    /* Number of valid bytes in data */
    uint8_t port;    /* MIDI port index */
    uint8_t reserved[2];
    uint8_t data[PW_ASIO_MIDI_MAX_DATA];
} PWASIO_MidiEvent;

typedef struct PWASIO_MidiBuffer {
    uint32_t capacity;
    uint32_t count;
    PWASIO_MidiEvent *events;
} PWASIO_MidiBuffer;

typedef struct PWASIO_MidiInfo {
    uint32_t api_version;
    uint32_t num_inputs;
    uint32_t num_outputs;
} PWASIO_MidiInfo;

#ifdef __cplusplus
}
#endif
