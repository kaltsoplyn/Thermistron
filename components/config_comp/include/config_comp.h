#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"

#define MAX_CONFIG_UPDATE_CALLBACKS     3
#define DEFAULT_MEASUREMENT_INTERVAL_MS 10000
#define MIN_SAMPLING_INTERVAL_MS        1000
#define MAX_THERMISTOR_COUNT            6
#define ADC_BITWIDTH                    ADC_BITWIDTH_12
#define ADC_ATTENUATION                 ADC_ATTEN_DB_12  // Supposedely ADC_ATTEN_DB_12 → 150 mV ~ 2450 mV
#define ADC_UNIT_ID                     ADC_UNIT_1

// NOTE: bitwidth and attenuation really go to the channel measurement in the sensor components:
//adc_oneshot_chan_cfg_t channel_config = {
//        .bitwidth = ADC_BITWIDTH,
//        .atten = ADC_ATTENUATION
//    };

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*config_update_callback_t)(void);

typedef struct {
    char    name[10];
    int     divider_resistor_value;         // Ohm
    int     calibration_resistance_offset;  // Ohm
    int     adc_channel;
} ThermistorConfig_t;


typedef struct {
    int     sampling_interval_ms;                               // Default: 10000, Min: 1000
    bool    serial_stream_active;                               // Default: false
    bool    log_temp_measurements;                              // Default: false -> whether to log temperatures to console 
    int     thermistor_count;                                   // Number of active thermistors
    ThermistorConfig_t thermistors[MAX_THERMISTOR_COUNT];       // Array of thermistor pin names
    adc_oneshot_unit_handle_t adc_unit_handle; 
} AppConfig_t;


esp_err_t config_comp_init(void);

esp_err_t config_comp_get_app_config(AppConfig_t *app_config);

esp_err_t config_comp_set_sampling_interval(int sampling_interval_ms);
int config_comp_get_sampling_interval();

esp_err_t config_comp_set_serial_stream_active(bool active);
bool config_comp_get_serial_stream_active();

esp_err_t config_comp_set_log_temps_active(bool active);
bool config_comp_get_log_temps_active();

esp_err_t config_comp_update_thermistor_count();
int config_comp_get_thermistor_count();

esp_err_t config_comp_set_thermistor_config(int index, const ThermistorConfig_t *config);
esp_err_t config_comp_get_thermistor_config(int index, ThermistorConfig_t *config);

esp_err_t config_comp_set_calibration_resistance_offset(int index, int offset);
esp_err_t config_comp_get_calibration_resistance_offset(int index, int *offset);

esp_err_t config_comp_get_adc_unit_handle(adc_oneshot_unit_handle_t *adc_unit_handle);

esp_err_t config_comp_register_update_callback(config_update_callback_t callback);
esp_err_t config_comp_unregister_update_callback(config_update_callback_t callback);


#ifdef __cplusplus
}
#endif