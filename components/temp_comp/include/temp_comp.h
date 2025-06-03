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

esp_err_t temp_comp_init(void);

void temp_comp_measurement_task(void *arg);

esp_err_t temp_comp_get_measurement_data_json(char *buffer, size_t buffer_size);



#ifdef __cplusplus
}
#endif