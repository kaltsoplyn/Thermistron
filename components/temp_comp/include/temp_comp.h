#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "config_comp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char thermistor_names[MAX_THERMISTOR_COUNT][10];
    float temperatures[MAX_THERMISTOR_COUNT];
} TemperatureOutputData_t;

// typedef struct {
//     TemperatureData_t temperature_data[MAX_THERMISTOR_COUNT]; //MAX_THERMISTOR_COUNT defined in config_comp.h
// } MeasurementData_t;

/**
 * @brief Initialize the temperature compensation module.
 *
 * This function sets up any required hardware or software resources for temperature compensation.
 *
 * @return
 *      - ESP_OK on success
 *      - Appropriate error code otherwise
 */
esp_err_t temp_comp_init(void);

/**
 * @brief Task function for periodic temperature measurement.
 *
 * This FreeRTOS task periodically measures temperature and updates internal state.
 *
 * @param arg Pointer to task parameters (unused, can be NULL).
 */
void temp_comp_measurement_task(void *arg);

/**
 * @brief Get the latest temperature readings in JSON format.
 *
 * This function writes the most recent temperature measurements into the provided buffer as a JSON string.
 *
 * @param buffer Pointer to the buffer where the JSON string will be written.
 * @param buffer_size Size of the buffer in bytes.
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if buffer is NULL or buffer_size is insufficient
 *      - Appropriate error code otherwise
 */
esp_err_t temp_comp_get_latest_temps_json(char *buffer, size_t buffer_size);

/**
 * @brief Refresh cached configuration and ADC readings.
 *
 * This function reloads configuration parameters and updates cached ADC values used for temperature compensation.
 *
 * @return
 *      - ESP_OK on success
 *      - Appropriate error code otherwise
 */
esp_err_t temp_comp_refresh_cached_config_and_adc();

#ifdef __cplusplus
}
#endif