#include "config_comp.h"
#include "freertos/FreeRTOS.h" // Required for mutex
#include "freertos/semphr.h"  // Required for mutex
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config_comp";
static AppConfig_t s_app_config;
static SemaphoreHandle_t s_config_mutex = NULL;

esp_err_t config_comp_init() {
    esp_err_t ret = ESP_OK;

    s_config_mutex = xSemaphoreCreateMutex();
    if (s_config_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return ESP_FAIL; // Or a more specific error
    }

    xSemaphoreTake(s_config_mutex, portMAX_DELAY);

    // Initialize the application configuration with default values
    s_app_config.sampling_interval_ms = DEFAULT_MEASUREMENT_INTERVAL_MS;
    s_app_config.serial_stream_active = false;
    s_app_config.thermistor_count = 5;

    const ThermistorConfig_t thermistors[MAX_THERMISTOR_COUNT] = {

        {"Therm1",  9782, ADC_CHANNEL_0}, // Example values
        {"Therm2",  9795, ADC_CHANNEL_1},
        {"Therm3",  9888, ADC_CHANNEL_2},
        {"Therm4",  9963, ADC_CHANNEL_3},
        {"Therm5", 10233, ADC_CHANNEL_4},
        {"UNUSED", 10000, ADC_CHANNEL_5} // <-- unused slot
    };

    memcpy(s_app_config.thermistors, thermistors, sizeof(thermistors));


    // Initialize ADC unit handle
    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = ADC_UNIT_ID,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    ret = adc_oneshot_new_unit(&adc_init_cfg, &s_app_config.adc_unit_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_config_mutex);
        return ret;
    }

    ESP_LOGI(TAG, "Initial configuration completed successfully");
    xSemaphoreGive(s_config_mutex);

    return ESP_OK;
}

esp_err_t config_comp_get_app_config(AppConfig_t *app_config) {
    if (app_config == NULL) {
        ESP_LOGE(TAG, "Provided app_config pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    memcpy(app_config, &s_app_config, sizeof(AppConfig_t));
    xSemaphoreGive(s_config_mutex);
    return ESP_OK;
}

esp_err_t config_comp_set_sampling_interval(int sampling_interval_ms){
    if (sampling_interval_ms < MIN_SAMPLING_INTERVAL_MS) {
        ESP_LOGE(TAG, "Sampling interval must be at least %d ms", MIN_SAMPLING_INTERVAL_MS);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    s_app_config.sampling_interval_ms = sampling_interval_ms;
    xSemaphoreGive(s_config_mutex);
    ESP_LOGI(TAG, "Sampling interval set to %d ms", sampling_interval_ms);
    return ESP_OK;
}

int config_comp_get_sampling_interval() {
    int interval;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    interval = s_app_config.sampling_interval_ms;
    xSemaphoreGive(s_config_mutex);
    return interval;
}

esp_err_t config_comp_set_serial_stream_active(bool active) {
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    s_app_config.serial_stream_active = active;
    xSemaphoreGive(s_config_mutex);
    ESP_LOGI(TAG, "Serial stream is now %s", active ? "active" : "inactive");
    return ESP_OK;
}

bool config_comp_get_serial_stream_active() {
    bool active;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    active = s_app_config.serial_stream_active;
    xSemaphoreGive(s_config_mutex);
    return active;
}

esp_err_t config_comp_update_thermistor_count() {
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    int count = MAX_THERMISTOR_COUNT;
    for (int i = 0; i < MAX_THERMISTOR_COUNT; i++) {
        char *name = s_app_config.thermistors[i].name;
        if (name[0] == '\0' || strcmp(name, "UNUSED") == 0) {
            count -= 1;
        }
    }
    s_app_config.thermistor_count = count;
    xSemaphoreGive(s_config_mutex);
    ESP_LOGI(TAG, "Thermistor count updated to %d", count);
    return ESP_OK;
}

int config_comp_get_thermistor_count() {
    int count;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    count = s_app_config.thermistor_count;
    xSemaphoreGive(s_config_mutex);
    return count;
}

esp_err_t config_comp_set_thermistor_config(int index, const ThermistorConfig_t *config){

    if (index < 0 || index > MAX_THERMISTOR_COUNT - 1) {
        ESP_LOGE(TAG, "Thermistor index %d is out of bounds", index);
        return ESP_ERR_INVALID_ARG;
    }
    if (config == NULL) {
        ESP_LOGE(TAG, "Provider thermistor config pointer provided is null");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    memcpy(&s_app_config.thermistors[index], config, sizeof(ThermistorConfig_t));
    xSemaphoreGive(s_config_mutex);
    ESP_LOGI(TAG, "Thermistor %d configuration updated: %s, Resistor: %d, ADC Channel: %d",
             index, config->name, config->divider_resistor_value, config->adc_channel);
    return ESP_OK;
}

esp_err_t config_comp_get_thermistor_config(int index, ThermistorConfig_t *config) {
    if (index < 0 || index > MAX_THERMISTOR_COUNT - 1) {
        ESP_LOGE(TAG, "Thermistor index %d is out of bounds", index);
        return ESP_ERR_INVALID_ARG;
    }
    if (config == NULL) {
        ESP_LOGE(TAG, "Provider thermistor config pointer provided is null");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    memcpy(config, &s_app_config.thermistors[index], sizeof(ThermistorConfig_t));
    xSemaphoreGive(s_config_mutex);
    return ESP_OK;
}

esp_err_t config_comp_get_adc_unit_handle(adc_oneshot_unit_handle_t *adc_unit_handle) {
    if (adc_unit_handle == NULL) {
        ESP_LOGE(TAG, "Provided adc_unit_handle pointer is null");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    *adc_unit_handle = s_app_config.adc_unit_handle;
    xSemaphoreGive(s_config_mutex);
    ESP_LOGI(TAG, "Retrieved ADC unit handle");
    return ESP_OK;
}