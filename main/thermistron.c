/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "config_comp.h"
#include "temp_comp.h"
#include "serial_comp.h"

static const char *TAG = "thermistron_main";

void task_temperature_measurement(void *arg) {
    while (1) {
        // implement measurement business logic here
        vTaskDelay(pdMS_TO_TICKS(DEFAULT_MEASUREMENT_INTERVAL_MS));
    }
}

void app_main(void)
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    fflush(stdout);

    esp_err_t ret = config_comp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize config component: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Configuration component initialized successfully");

    ret = temp_comp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize temperature measurement component: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Temperature measurement component initialized successfully");

    ret = serial_comp_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize serial communication component: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Serial communication component initialized successfully");


    ESP_LOGI(TAG, "Initialization complete");

    xTaskCreate(temp_comp_measurement_task, "temperature_measurement_task", 4096, NULL, 5, NULL);
    xTaskCreate(serial_comp_task, "serial_comp_task", SERIAL_STACK_SIZE, NULL, 4, NULL);

}
