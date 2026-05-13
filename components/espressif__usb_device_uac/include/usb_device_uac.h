/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*uac_output_cb_t)(uint8_t *buf, size_t len, void *cb_ctx);
typedef esp_err_t (*uac_input_cb_t)(uint8_t *buf, size_t len, size_t *bytes_read, void *cb_ctx);
typedef void (*uac_set_mute_cb_t)(uint32_t mute, void *cb_ctx);
typedef void (*uac_set_volume_cb_t)(uint32_t volume, void *cb_ctx);

/**
 * @brief USB UAC Device Config
 *
 */
typedef struct  {
    bool skip_tinyusb_init;                      /*!< if true, the Tinyusb and usb phy will not be initialized */
    uac_output_cb_t output_cb;                   /*!< callback function for UAC data output, if NULL, output will be disabled */
    uac_input_cb_t input_cb;                     /*!< callback function for UAC data input, if NULL, input will be disabled */
    uac_set_mute_cb_t set_mute_cb;               /*!< callback function for set mute, if NULL, the set mute request will be ignored */
    uac_set_volume_cb_t set_volume_cb;           /*!< callback function for set volume, if NULL, the set volume request will be ignored */
    void *cb_ctx;                                /*!< callback context, for user specific usage */
#if CONFIG_USB_DEVICE_UAC_AS_PART
    int spk_itf_num;                             /*!< If CONFIG_USB_DEVICE_UAC_AS_PART is enabled, you need to provide the speaker interface number */
    int mic_itf_num;                             /*!< If CONFIG_USB_DEVICE_UAC_AS_PART is enabled, you need to provide the mic interface number */
#endif
} uac_device_config_t;

/**
 * @brief Initialize the USB Audio Class (UAC) device.
 *
 * @param config Pointer to the UAC device configuration structure.
 * @return
 *       - ESP_OK on success
 *       - ESP_FAIL on failure
 */
esp_err_t uac_device_init(uac_device_config_t *config);

/**
 * @brief Update the speaker feature-unit volume exposed to the USB host.
 *
 * This updates the value returned by UAC GET_CUR without touching PCM samples.
 * The hardware/application is still responsible for applying the actual gain.
 *
 * @param volume Volume in percent, 0..100.
 * @return ESP_OK on success.
 */
esp_err_t uac_device_set_volume_percent(uint32_t volume);

/**
 * @brief Update the speaker feature-unit mute state exposed to the USB host.
 *
 * @param mute true to mute, false to unmute.
 * @return ESP_OK on success.
 */
esp_err_t uac_device_set_mute_state(bool mute);

/**
 * @brief Send one HID consumer-control key press to the USB host.
 *
 * This is used for host-visible media keys such as volume up/down and mute.
 * It does not change PCM samples or codec gain.
 *
 * @param usage HID consumer usage, for example 0x00E9 volume increment.
 * @return ESP_OK on success.
 */
esp_err_t uac_device_send_hid_consumer_control(uint16_t usage);

#ifdef __cplusplus
}
#endif
