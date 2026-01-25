#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "pw_asio_midi.h"

#ifndef PW_ASIO_MIDI_MAX_EVENTS
#define PW_ASIO_MIDI_MAX_EVENTS 1024
#endif

#ifndef PW_ASIO_MIDI_MAX_DATA
#define PW_ASIO_MIDI_MAX_DATA 4
#endif

// Maximum MIDI events per buffer (should be enough for even dense MIDI)
#define MAX_MIDI_EVENTS_PER_BUFFER PW_ASIO_MIDI_MAX_EVENTS

// MIDI event structure - stores timestamped MIDI data
struct midi_event {
	uint32_t offset;      // Sample offset within current buffer
	uint8_t size;         // Number of valid bytes in data
	uint8_t port;         // MIDI port index
	uint8_t reserved[2];
	uint8_t data[PW_ASIO_MIDI_MAX_DATA]; // MIDI data bytes (max 4 bytes for most messages)
};

// MIDI buffer - ring buffer for events between PipeWire and ASIO callbacks
struct midi_buffer {
	struct midi_event events[MAX_MIDI_EVENTS_PER_BUFFER];
	uint32_t write_pos;   // Next position to write
	uint32_t read_pos;    // Next position to read
	uint32_t count;       // Number of events currently in buffer
	pthread_mutex_t lock; // Lock for thread-safe access
};

// Initialize MIDI buffer
static inline void midi_buffer_init(struct midi_buffer *buf) {
	buf->write_pos = 0;
	buf->read_pos = 0;
	buf->count = 0;
	pthread_mutex_init(&buf->lock, NULL);
}

// Destroy MIDI buffer
static inline void midi_buffer_destroy(struct midi_buffer *buf) {
	pthread_mutex_destroy(&buf->lock);
}

// Clear all events from buffer
static inline void midi_buffer_clear(struct midi_buffer *buf) {
	pthread_mutex_lock(&buf->lock);
	buf->write_pos = 0;
	buf->read_pos = 0;
	buf->count = 0;
	pthread_mutex_unlock(&buf->lock);
}

// Add MIDI event to buffer (returns false if buffer full)
static inline bool midi_buffer_write(struct midi_buffer *buf, uint8_t port, uint32_t offset,
                                     const uint8_t *data, uint8_t size) {
	pthread_mutex_lock(&buf->lock);

	if (buf->count >= MAX_MIDI_EVENTS_PER_BUFFER) {
		pthread_mutex_unlock(&buf->lock);
		return false; // Buffer full
	}

	struct midi_event *ev = &buf->events[buf->write_pos];
	ev->offset = offset;
	ev->size = size > PW_ASIO_MIDI_MAX_DATA ? PW_ASIO_MIDI_MAX_DATA : size; // Clamp to max bytes
	ev->port = port;
	for (uint8_t i = 0; i < ev->size; i++) {
		ev->data[i] = data[i];
	}

	buf->write_pos = (buf->write_pos + 1) % MAX_MIDI_EVENTS_PER_BUFFER;
	buf->count++;

	pthread_mutex_unlock(&buf->lock);
	return true;
}

// Try to add MIDI event to buffer without blocking (returns false if locked or full)
static inline bool midi_buffer_try_write(struct midi_buffer *buf, uint8_t port, uint32_t offset,
                                         const uint8_t *data, uint8_t size) {
	if (pthread_mutex_trylock(&buf->lock) != 0) {
		return false;
	}

	if (buf->count >= MAX_MIDI_EVENTS_PER_BUFFER) {
		pthread_mutex_unlock(&buf->lock);
		return false; // Buffer full
	}

	struct midi_event *ev = &buf->events[buf->write_pos];
	ev->offset = offset;
	ev->size = size > PW_ASIO_MIDI_MAX_DATA ? PW_ASIO_MIDI_MAX_DATA : size; // Clamp to max bytes
	ev->port = port;
	for (uint8_t i = 0; i < ev->size; i++) {
		ev->data[i] = data[i];
	}

	buf->write_pos = (buf->write_pos + 1) % MAX_MIDI_EVENTS_PER_BUFFER;
	buf->count++;

	pthread_mutex_unlock(&buf->lock);
	return true;
}

// Read MIDI event from buffer (returns false if buffer empty)
static inline bool midi_buffer_read(struct midi_buffer *buf, struct midi_event *out) {
	pthread_mutex_lock(&buf->lock);

	if (buf->count == 0) {
		pthread_mutex_unlock(&buf->lock);
		return false; // Buffer empty
	}

	*out = buf->events[buf->read_pos];
	buf->read_pos = (buf->read_pos + 1) % MAX_MIDI_EVENTS_PER_BUFFER;
	buf->count--;

	pthread_mutex_unlock(&buf->lock);
	return true;
}

// Try to read MIDI event without blocking (returns false if locked or empty)
static inline bool midi_buffer_try_read(struct midi_buffer *buf, struct midi_event *out) {
	if (pthread_mutex_trylock(&buf->lock) != 0) {
		return false;
	}

	if (buf->count == 0) {
		pthread_mutex_unlock(&buf->lock);
		return false; // Buffer empty
	}

	*out = buf->events[buf->read_pos];
	buf->read_pos = (buf->read_pos + 1) % MAX_MIDI_EVENTS_PER_BUFFER;
	buf->count--;

	pthread_mutex_unlock(&buf->lock);
	return true;
}

// Get number of events currently in buffer (thread-safe)
static inline uint32_t midi_buffer_count(struct midi_buffer *buf) {
	pthread_mutex_lock(&buf->lock);
	uint32_t count = buf->count;
	pthread_mutex_unlock(&buf->lock);
	return count;
}

#ifdef __cplusplus
}
#endif
