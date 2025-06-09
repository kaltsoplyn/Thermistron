#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "temp_comp.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "temp_comp";

static ThermistorConfig_t s_cached_therm_configs[MAX_THERMISTOR_COUNT];
static int s_cached_active_therm_count = 0;
static int s_cached_sampling_interval_ms = DEFAULT_MEASUREMENT_INTERVAL_MS; // Default from config_comp.h
static bool s_log_temp_measurements = false;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

// static char temp_buffer[2048] = {0}; //TEMPORARY for DEBUGGING

static float s_latest_temperatures[MAX_THERMISTOR_COUNT];
static SemaphoreHandle_t s_temp_data_mutex = NULL;

static volatile bool s_config_needs_refresh = false;

static const adc_oneshot_chan_cfg_t s_channel_config = {
    .bitwidth = ADC_BITWIDTH,
    .atten = ADC_ATTENUATION,
};

esp_err_t temp_comp_refresh_cached_config_and_adc() {
    esp_err_t ret;

    // If ADC unit is re-initialized by config_comp, old handle is invalid.
    ret = config_comp_get_adc_unit_handle(&s_adc_handle);
    if (ret != ESP_OK || s_adc_handle == NULL) {
        ESP_LOGE(TAG, "[CACHE REFRESH] Failed to get ADC unit handle: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[CACHE REFRESH] ADC unit handle obtained.");

    s_cached_sampling_interval_ms = config_comp_get_sampling_interval();
    ESP_LOGI(TAG, "[CACHE REFRESH] Using sampling interval: %d ms", s_cached_sampling_interval_ms);

    s_log_temp_measurements = config_comp_get_log_temps_active();
    ESP_LOGI(TAG, "[CACHE REFRESH] Measured temperatures will%sbe logged to console", s_log_temp_measurements ? " " : " not ");

    s_cached_active_therm_count = config_comp_get_thermistor_count();
    if (s_cached_active_therm_count < 0 || s_cached_active_therm_count > MAX_THERMISTOR_COUNT) {
        ESP_LOGW(TAG, "[CACHE REFRESH] Invalid thermistor count from config: %d.", s_cached_active_therm_count);
        // Continue, but log warning. The loop below will correctly identify active ones.
    }
    ESP_LOGI(TAG, "[CACHE REFRESH] Expecting %d active thermistors.", s_cached_active_therm_count);

    for (int i = 0; i < MAX_THERMISTOR_COUNT; ++i) {
        ret = config_comp_get_thermistor_config(i, &s_cached_therm_configs[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[CACHE REFRESH] Failed to get config for thermistor %d: %s", i, esp_err_to_name(ret));
            continue; // Skip this thermistor config if fetch fails
        }
        char *name = s_cached_therm_configs[i].name;
        if (name[0] == '\0' || strcmp(name, "UNUSED") == 0) {
            continue; // Skip also if UNUSED
        }

        ret = adc_oneshot_config_channel(s_adc_handle, s_cached_therm_configs[i].adc_channel, &s_channel_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[CACHE REFRESH] Failed to configure ADC channel %d for thermistor %s: %s",
                     s_cached_therm_configs[i].adc_channel, s_cached_therm_configs[i].name, esp_err_to_name(ret));
        }
    }
    ESP_LOGI(TAG, "[CACHE REFRESH] Complete");
    return ESP_OK;

}

static void handle_config_update_notification(void) {
    ESP_LOGI(TAG, "Received configuration update notification.");
    s_config_needs_refresh = true;
}

esp_err_t temp_comp_init() {
    ESP_LOGI(TAG, "Initializing temperature component...");
    esp_err_t ret;

    s_temp_data_mutex = xSemaphoreCreateMutex();
    if (s_temp_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create temperature data mutex");
        return ESP_FAIL;
    }

    ret = temp_comp_refresh_cached_config_and_adc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initial configuration cache refresh failed.");
        // Mutex for temp data is created, but component init fails.
        vSemaphoreDelete(s_temp_data_mutex);
        s_temp_data_mutex = NULL;
        return ESP_FAIL;
    }

    ret = config_comp_register_update_callback(handle_config_update_notification);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register for configuration updates.");
        // Not fatal if initial cache is good, but no updates.
        // return ESP_FAIL if this is not acceptable
    }

    // // Get thermistor configurations and count
    // s_cached_active_therm_count = config_comp_get_thermistor_count();
    // if (s_cached_active_therm_count < 0 || s_cached_active_therm_count > MAX_THERMISTOR_COUNT) {
    //     ESP_LOGE(TAG, "Invalid thermistor count: %d", s_cached_active_therm_count);
    //     return ESP_ERR_INVALID_STATE;
    // }
    // ESP_LOGI(TAG, "Expecting %d active thermistors.", s_cached_active_therm_count);

    // for (int i = 0; i < MAX_THERMISTOR_COUNT; ++i) {
    //     ret = config_comp_get_thermistor_config(i, &s_cached_therm_configs[i]);
    //     if (ret != ESP_OK) {
    //         ESP_LOGE(TAG, "Failed to get config for thermistor %d: %s", i, esp_err_to_name(ret));
    //         return ret;
    //     }
        
    //     char *name = s_cached_therm_configs[i].name;
    //     if (name[0] == '\0' || strcmp(name, "UNUSED") == 0) {
    //         continue;
    //     }

    //     // Configure the ADC channel for this thermistor
    //     ret = adc_oneshot_config_channel(s_adc_handle, s_cached_therm_configs[i].adc_channel, &s_channel_config);
    //     if (ret != ESP_OK) {
    //         ESP_LOGE(TAG, "Failed to configure ADC channel %d for thermistor %s: %s",
    //                  s_cached_therm_configs[i].adc_channel, s_cached_therm_configs[i].name, esp_err_to_name(ret));
    //         return ret;
    //     }
    //     ESP_LOGI(TAG, "Configured thermistor %s (ADC Channel %d)", s_cached_therm_configs[i].name, s_cached_therm_configs[i].adc_channel);
    // }

    memset(s_latest_temperatures, 0, sizeof(s_latest_temperatures)); // Initialize temperatures

    ESP_LOGI(TAG, "Temperature component initialized successfully.");
    return ESP_OK;
}

static uint32_t get_max_adc_value_from_enum(adc_bitwidth_t bitwidth_enum) {
    switch (bitwidth_enum) {
        case ADC_BITWIDTH_9:  return (1 << 9) - 1;
        case ADC_BITWIDTH_10: return (1 << 10) - 1;
        case ADC_BITWIDTH_11: return (1 << 11) - 1;
        case ADC_BITWIDTH_12: return (1 << 12) - 1;
        // ADC_BITWIDTH_DEFAULT is usually 12 on ESP32 series
        case ADC_BITWIDTH_DEFAULT: return (1 << 12) - 1;
        default:
            ESP_LOGW(TAG, "Unknown ADC bitwidth enum %d, assuming 12-bit (4095 max)", bitwidth_enum);
            return (1 << 12) - 1; // Default to 12-bit max value
    }
}

static esp_err_t _read_adc_value(ThermistorConfig_t *thermistor, int *out_raw_value) {
    if (out_raw_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle is not initialized for reading.");
        return ESP_ERR_INVALID_STATE;
    }

    adc_channel_t channel = (adc_channel_t)thermistor->adc_channel;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, channel, out_raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed on channel %d: %s", channel, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t _measure_temperature(ThermistorConfig_t *thermistor, float *out_temperature) {
    if (out_temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int adc_value;
    esp_err_t ret = _read_adc_value(thermistor, &adc_value);
    if (ret != ESP_OK) {
        *out_temperature = NAN;
        return ret;
    }
    int divider_resistor = thermistor->divider_resistor_value;
    int calibration_offset = thermistor->calibration_resistance_offset;
    uint32_t max_adc_val = get_max_adc_value_from_enum(s_channel_config.bitwidth);

    if (adc_value <= 0 || adc_value >= max_adc_val) {
        ESP_LOGW(TAG, "ADC value %d for %s is at or beyond limits (0, %"PRIu32"). Temp calculation may be inaccurate or NAN.", adc_value, thermistor->name, max_adc_val);
        // For adc_value == 0, Rth -> 0. For adc_value == max_adc_val, Rth -> infinity.
        // Steinhart-Hart is not well-behaved at these extremes.
        if (adc_value <= 0) *out_temperature = HUGE_VALF; // Effectively very cold (Rth near 0)
        else *out_temperature = -HUGE_VALF; // Effectively very hot (Rth near inf, 1/T -> 0, T -> inf, so -273.15 for Celsius)
                                          // Or simply NAN for out of range. Let's use NAN for simplicity of interpretation.
        *out_temperature = NAN;
        return ESP_ERR_INVALID_STATE;
    }

    float Rth = (float)divider_resistor * adc_value / (max_adc_val - adc_value) + calibration_offset;

    if (Rth <= 0) { // Should not happen if adc_value is within (0, max_adc_val)
        ESP_LOGE(TAG, "Calculated Rth <= 0 (%.2f) for %s, cannot compute log.", Rth, thermistor->name);
        *out_temperature = NAN;
        return ESP_ERR_INVALID_STATE;
    }

    // Steinhart-Hart coefficients
    const float A = 0.001129148f;
    const float B = 0.000234125f;
    const float C = 0.0000000876741f;

    float logRth = logf(Rth);
    float temp_k = 1.0f / (A + B * logRth + C * logRth * logRth * logRth);
    *out_temperature = temp_k - 273.15f; // Convert Kelvin to Celsius

    if (s_log_temp_measurements) {
        ESP_LOGI(TAG, "Thermistor %s: ADC %d, Rth %.2f Ohm (incl. calibration offset: %d Ohm), Temp: %.2f C", thermistor->name, adc_value, Rth, calibration_offset, *out_temperature);
    }

    return ESP_OK;
}

void temp_comp_measurement_task(void *arg) {
    ESP_LOGI(TAG, "Temperature measurement task started");
    while (1) {
        if (s_config_needs_refresh) {
            ESP_LOGI(TAG, "Configuration change detected, refreshing cache...");
            if (temp_comp_refresh_cached_config_and_adc() == ESP_OK) {
                s_config_needs_refresh = false; // Clear the flag only on success
                ESP_LOGI(TAG, "Cache refreshed successfully.");
            } else {
                ESP_LOGE(TAG, "Failed to refresh cache. Will retry on next cycle.");
            }
        }

        for (int i = 0; i < MAX_THERMISTOR_COUNT; ++i) {

            char *name = s_cached_therm_configs[i].name;
            if (name[0] == '\0' || strcmp(name, "UNUSED") == 0) {
                continue;
            }

            float current_temp_val = NAN; // Default to NAN
            esp_err_t meas_ret = _measure_temperature(&s_cached_therm_configs[i], &current_temp_val);

            if (meas_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to measure temperature for %s: %s. Storing NAN.",
                         s_cached_therm_configs[i].name, esp_err_to_name(meas_ret));
                // current_temp_val is already NAN or set by _measure_temperature on error
            }

            if (xSemaphoreTake(s_temp_data_mutex, portMAX_DELAY) == pdTRUE) {
                s_latest_temperatures[i] = current_temp_val;
                xSemaphoreGive(s_temp_data_mutex);
            }
        }
        // if (s_log_temp_measurements) {
        //     temp_comp_get_latest_temps_json(temp_buffer, 2048);
        //     ESP_LOGI(TAG, "Latest temperatures JSON: %s", temp_buffer);
        // }
        vTaskDelay(pdMS_TO_TICKS(s_cached_sampling_interval_ms));
    }
}

esp_err_t temp_comp_get_latest_temps_json(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer or buffer size");
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = '\0'; // Start with an empty string
    size_t current_len = 0;
    int written = 0;

    // Acquire mutex to read latest temperatures
    if (xSemaphoreTake(s_temp_data_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take temperature data mutex");
        return ESP_FAIL; // Or appropriate error
    }

    // Start the main JSON object
    written = snprintf(buffer + current_len, buffer_size - current_len, "{\"names\":[");
    if (written < 0 || written >= buffer_size - current_len) {
        goto fail_buffer_too_small;
    }
    current_len += written;

    // Add thermistor names
    bool first_name = true;
    for (int i = 0; i < MAX_THERMISTOR_COUNT; ++i) {
        char *name = s_cached_therm_configs[i].name;
        if (name[0] != '\0' && strcmp(name, "UNUSED") != 0) {
            if (!first_name) {
                written = snprintf(buffer + current_len, buffer_size - current_len, ",");
                if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
                current_len += written;
            }
            written = snprintf(buffer + current_len, buffer_size - current_len, "\"%s\"", name);
            if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
            current_len += written;
            first_name = false;
        }
    }

    // Close names array and start temperatures array
    written = snprintf(buffer + current_len, buffer_size - current_len, "],\"temperatures\":[");
    if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
    current_len += written;

    // Add temperatures
    bool first_temp = true;
    for (int i = 0; i < MAX_THERMISTOR_COUNT; ++i) {
         char *name = s_cached_therm_configs[i].name;
        if (name[0] != '\0' && strcmp(name, "UNUSED") != 0) {
            if (!first_temp) {
                written = snprintf(buffer + current_len, buffer_size - current_len, ",");
                if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
                current_len += written;
            }
            // Use %.2f for temperature formatting (2 decimal places)
            written = snprintf(buffer + current_len, buffer_size - current_len, "%.2f", s_latest_temperatures[i]);
            if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
            current_len += written;
            first_temp = false;
        }
    }

    // Close temperatures array and main object
    written = snprintf(buffer + current_len, buffer_size - current_len, "]}");
    if (written < 0 || written >= buffer_size - current_len) goto fail_buffer_too_small;
    current_len += written;

    // Release mutex
    xSemaphoreGive(s_temp_data_mutex);

    // ESP_LOGD(TAG, "Generated JSON: %s", buffer);
    return ESP_OK;

    fail_buffer_too_small:
        xSemaphoreGive(s_temp_data_mutex);
        ESP_LOGE(TAG, "Buffer too small for JSON output");
        // Ensure buffer is null-terminated even on failure if possible
        if (buffer_size > 0) buffer[0] = '\0';
        return ESP_ERR_NO_MEM;

}
