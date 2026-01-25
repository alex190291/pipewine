#pragma once

#ifndef GUI_API
#define GUI_API
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @file gui.h
 * @brief PipeWine ASIO GUI interface
 *
 * This header defines the interface between the ASIO driver and its
 * Qt-based settings dialog.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct pwasio_gui;

/**
 * @brief GUI configuration structure
 *
 * Shared between the GUI and its client (the ASIO driver).
 * The driver fills in the callbacks and configuration values,
 * and the GUI updates them when the user makes changes.
 */
struct pwasio_gui_conf {
	void *user;  /**< Client's state pointer (typically IWineASIOImpl*) */

	/**
	 * @brief Called when GUI wants to close
	 * Called after apply_config if user confirmed. The application
	 * should call pwasio_destroy_gui in response.
	 */
	void (*closed)(struct pwasio_gui_conf *gui);

	/** @brief Apply configuration changes */
	void (*apply_config)(struct pwasio_gui_conf *gui);

	/** @brief Load current configuration into this struct */
	void (*load_config)(struct pwasio_gui_conf *gui);

	struct user_pw_helper *pw_helper;  /**< PipeWire helper instance */

	/* Configuration values */
	uint32_t cf_buffer_size;      /**< Buffer size in samples */
	uint32_t cf_sample_rate;      /**< Sample rate in Hz */
	uint32_t cf_input_channels;   /**< Number of input channels */
	uint32_t cf_output_channels;  /**< Number of output channels */
	bool cf_auto_connect;         /**< Auto-connect to hardware */
};

// The config must persist for as long as the GUI is live.
GUI_API struct pwasio_gui *pwasio_init_gui(struct pwasio_gui_conf *conf);
GUI_API void pwasio_destroy_gui(struct pwasio_gui *gui);

#define GUI_LIB_NAME "libpwasio_gui.so"

#ifdef __cplusplus
}
#endif
