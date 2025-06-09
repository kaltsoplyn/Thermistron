#include "serial_comp.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "config_comp.h"
#include "temp_comp.h"
// #include <ctype.h>

#define RECEIVE_CHUNK_SIZE 64

#define MAX_COMMAND_LEN 128     // Maximum length for a command from serial
#define COMMAND_QUEUE_LENGTH 5  // How many commands can be buffered
static QueueHandle_t s_command_queue = NULL;
static TaskHandle_t s_serial_rx_task_handle = NULL;

static const char *TAG = "serial_comp";

static char s_serial_buffer[SERIAL_BUFFER_SIZE] = {0};

esp_err_t serial_comp_init(void) {
    ESP_LOGI(TAG, "Initializing USB Serial/JTAG for standard blocking I/O...");

    s_command_queue = xQueueCreate(COMMAND_QUEUE_LENGTH, MAX_COMMAND_LEN);
    if (s_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_FAIL;
    }

    // Configuration for the USB Serial/JTAG driver
    // Default buffer sizes are usually sufficient.
    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .tx_buffer_size = SERIAL_BUFFER_SIZE, // TX buffer size
        .rx_buffer_size = SERIAL_BUFFER_SIZE, // RX buffer size
    };

    // Install the USB Serial/JTAG driver
    esp_err_t err = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB Serial/JTAG driver: %s", esp_err_to_name(err));
        return err;
    }

    // usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    // usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    // // 2. Tell VFS to use the driver for stdin/stdout/stderr
    // // This enables standard blocking behavior for reads.
    // usb_serial_jtag_vfs_use_driver();


    ESP_LOGI(TAG, "USB Serial/JTAG driver installed.");
    // Create the serial receiver task
    BaseType_t ret = xTaskCreate(serial_rx_task, "serial_rx_task", SERIAL_STACK_SIZE, NULL, 5, &s_serial_rx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial_rx_task");
        vQueueDelete(s_command_queue);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t serial_comp_send(const char* str) {
    int len = strlen(str);

    if (str == NULL || len == 0) {
        ESP_LOGE(TAG, "Cannot send NULL or empty string");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < len; i++) {
        usb_serial_jtag_write_bytes((uint8_t *)&str[i], 1, 20 / portTICK_PERIOD_MS);
    }
    char newline = '\n';
    usb_serial_jtag_write_bytes((uint8_t *)&newline, 1, 20 / portTICK_PERIOD_MS);

    // VFS implementation below:
    //printf("%s\n", str);
    // You might want to explicitly flush stdout if you're not sending newlines regularly
    // or if you experience buffering issues. Good practice for prompt sending.
    //fflush(stdout);
    ESP_LOGD(TAG, "Sent: %s", str);
    return ESP_OK;
}

int serial_comp_receive(char *buffer, int max_len) {
    if (buffer == NULL || max_len <= 0) {
        ESP_LOGE(TAG, "Invalid or uninitialized buffer for read");
        return -1;
    }

    uint8_t chunk_buffer[RECEIVE_CHUNK_SIZE];

    int buf_len = 0;
    buffer[0] = '\0';

    while (1) {
        // Read a chunk of bytes into the 'chunk_buffer'.
        // The second argument to usb_serial_jtag_read_bytes is the size of the buffer
        int bytes_read = usb_serial_jtag_read_bytes(chunk_buffer, RECEIVE_CHUNK_SIZE, 20 / portTICK_PERIOD_MS);

        if (bytes_read > 0) {
            for (int i = 0; i < bytes_read; i++) {
                
                char current_char = (char)chunk_buffer[i];
                usb_serial_jtag_write_bytes(&chunk_buffer[i], 1, 20 / portTICK_PERIOD_MS); // echo

                if (current_char == '\n' || current_char == '\r') {
                    // Some systems send CR on enter, some send LF or CRLF.
                    // (my windows send CR)

                    buffer[buf_len] = '\0'; // Null-terminate the string (excluding the newline)

                    return buf_len;

                } else {
                    // keep the -1 for the null terminator
                    if (buf_len < max_len - 1) {
                        buffer[buf_len] = current_char;
                        buf_len++;
                        
                        buffer[buf_len] = '\0'; // null-terminate always > good practice
                    } else {
                        // Buffer full
                        ESP_LOGE(TAG, "Line buffer overflow. Discarding current line fragment.");
                        // Discard and reset
                        buf_len = 0;
                        buffer[0] = '\0';
                        return -1;
                    }
                }
            }
        }
    }
}

static void _get_and_send_latest_temps_json(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "_get_and_send_latest_temps_json: Invalid buffer or buffer_size.");
        return;
    }

    // temp_comp_get_latest_temps_json is expected to null-terminate the buffer.
    esp_err_t ret = temp_comp_get_latest_temps_json(buffer, buffer_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get latest temperatures JSON: %s", esp_err_to_name(ret));
        buffer[0] = '\0'; // Ensure buffer is empty on error to prevent sending stale data
        return;
    }

    // If JSON was successfully generated and is not empty
    if (strlen(buffer) > 0) {
        ret = serial_comp_send(buffer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send temperatures JSON over serial: %s", esp_err_to_name(ret));
        }
    } else if (ret == ESP_OK) {
        // This case might occur if there are no active thermistors, resulting in an empty JSON object/array.
        ESP_LOGI(TAG, "Temperature JSON is empty, nothing to send.");
    }
}

void serial_rx_task(void *arg) {
    char command_buffer[MAX_COMMAND_LEN];
    ESP_LOGI(TAG, "Serial RX task started.");
    while(1) {
        int len = serial_comp_receive(command_buffer, MAX_COMMAND_LEN);
        if (len > 0) {
            // Send the received command to the queue
            if (xQueueSend(s_command_queue, command_buffer, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to send command to queue (queue full or timeout).");
            } else {
                ESP_LOGD(TAG, "Command '%s' sent to queue.", command_buffer);
            }
        } else if (len == 0) {
            // Timeout in serial_comp_receive, no full line yet, or empty line.
            // Can add a small delay here if serial_comp_receive could return 0 frequently
            // without blocking, but current serial_comp_receive blocks on read.
        } else { // len < 0
            ESP_LOGW(TAG, "Error or buffer overflow in serial_comp_receive. Resetting for next command.");
            // A small delay to prevent fast looping on persistent error.
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void serial_comp_task(void *arg) {
    char rcv_cmd[MAX_COMMAND_LEN];
    TickType_t queue_timeout_ticks;

    while(1) {
        queue_timeout_ticks = pdMS_TO_TICKS(config_comp_get_sampling_interval());

        if (xQueueReceive(s_command_queue, rcv_cmd, queue_timeout_ticks) == pdTRUE) { // cmd received
            ESP_LOGI(TAG, "Processing command: %s (raw len: %d)", rcv_cmd, strlen(rcv_cmd));

            // // --- Begin Detailed Debugging ---
            // const char* target_prefix_debug = "set sampling interval ";
            // int prefix_len_debug = strlen(target_prefix_debug); // Should be 23

            // ESP_LOGI(TAG, "Comparing rcv_cmd with target_prefix_debug: '%s' (len: %d)", target_prefix_debug, prefix_len_debug);
            // bool mismatch_found = false;
            // int compare_len = strlen(rcv_cmd) < prefix_len_debug ? strlen(rcv_cmd) : prefix_len_debug;

            // for (int k = 0; k < prefix_len_debug; ++k) { // Iterate up to the full prefix length
            //     if (k >= strlen(rcv_cmd) || rcv_cmd[k] != target_prefix_debug[k]) {
            //         ESP_LOGE(TAG, "Mismatch at index %d:", k);
            //         if (k < strlen(rcv_cmd)) {
            //             ESP_LOGE(TAG, "  rcv_cmd[%d] = 0x%02X ('%c')", k, (unsigned char)rcv_cmd[k], isprint((unsigned char)rcv_cmd[k]) ? rcv_cmd[k] : '?');
            //         } else {
            //             ESP_LOGE(TAG, "  rcv_cmd is shorter, ends at index %d", strlen(rcv_cmd) -1);
            //         }
            //         ESP_LOGE(TAG, "  target_prefix_debug[%d] = 0x%02X ('%c')", k, (unsigned char)target_prefix_debug[k], isprint((unsigned char)target_prefix_debug[k]) ? target_prefix_debug[k] : '?');
            //         mismatch_found = true;
            //         break;
            //     }
            // }
            // if (!mismatch_found) {
            //     ESP_LOGI(TAG, "Manual byte-by-byte comparison for prefix PASSED.");
            // }
            // // --- End Detailed Debugging ---
            
            if (strcmp(rcv_cmd, "help") == 0) {
                serial_comp_send(
                    "Available commands:\n"
                    "  help - Show this help message\n"
                    "  get temps - Get latest temperature readings in JSON format\n"
                    "  status - same as get temps\n"
                    "  toggle serial stream - Toggle streaming of temp measurements (taking place every 'sampling_interval_ms' ms) to the serial\n"
                    "  toggle temp log - Toggle logging of temperature measurements to the connected ESP32 device console\n"
                    "  force cache refresh - Force a refresh of the temperature component configuration and ADC channels\n"
                    "  set sampling interval <ms> - Set the sampling interval for temperature measurements (default is 1000 ms)\n"
                    "  get sampling interval - Get the current sampling interval in milliseconds\n"
                    "  incr cal res <index> - Increment the calibration resistance offset for a specific thermistor index (min index is 1)\n"
                    "  decr cal res <index> - Decrement the calibration resistance offset for a specific thermistor index (min index is 1)\n"
                    "  set cal res <index> <value> - Set the calibration resistance offset for a specific thermistor index (min index is 1)\n"
                );

            } else if (strcmp(rcv_cmd, "status") == 0 || strcmp(rcv_cmd, "get temps") == 0) {
                _get_and_send_latest_temps_json(s_serial_buffer, SERIAL_BUFFER_SIZE);
                ESP_LOGI("", "%s", s_serial_buffer);

            } else if (strcmp(rcv_cmd, "toggle serial stream") == 0) {
                bool current_serial_stream_state = config_comp_get_serial_stream_active();
                esp_err_t ret = config_comp_set_serial_stream_active(!current_serial_stream_state);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to toggle serial stream state. Error: %s", esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"serial_stream_active\":%s}", !current_serial_stream_state ? "true": "false");
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                } 

            } else if (strcmp(rcv_cmd, "toggle temp log") == 0) {
                bool current_log_temp_state = config_comp_get_log_temps_active();
                esp_err_t ret = config_comp_set_log_temps_active(!current_log_temp_state);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to toggle temperature logging at the device console.\nError: %s", esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"temp_log_active\":%s}", !current_log_temp_state ? "true": "false");
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                } 

            } else if (strcmp(rcv_cmd, "force cache refresh") == 0) {
                esp_err_t ret = config_comp_update_thermistor_count();
                ret = ret == ESP_OK ? temp_comp_refresh_cached_config_and_adc() : ret; 
                
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to force configuration refrech of the temperature measurement component.\nError: %s", esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"temp_component_cache_refresh_ok\":%s}", "true");
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                } 

            } else if (strncmp(rcv_cmd, "set sampling interval ", 22) == 0) {
                int new_interval = atoi(rcv_cmd + 22);
               
                esp_err_t ret = config_comp_set_sampling_interval(new_interval);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set new sampling interval: %s", esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"sampling_interval_ms\":%d}", new_interval);
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                } else {
                    ESP_LOGI(TAG, "Sampling interval set to %d ms", new_interval);
                }
                
            } else if (strcmp(rcv_cmd, "get sampling interval") == 0) {
                int ret = config_comp_get_sampling_interval();

                snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"sampling_interval_ms\":%d}", ret);
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                } else {
                    ESP_LOGI(TAG, "Sampling interval: %d ms", ret);
                }
                
            } else if (strncmp(rcv_cmd, "incr cal res ", 13) == 0) {
                int index = atoi(rcv_cmd + 13);
                int cal_R;

                esp_err_t ret = config_comp_incr_calibration_resistance_offset(index - 1);
                ret = ret == ESP_OK ? config_comp_get_calibration_resistance_offset(index - 1, &cal_R) : ret;
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to increment calibration resistance offset at index %d.\nError: %s", index, esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"index\":%d, \"cal_R\":%d}", index, cal_R);
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                }
                
            } else if (strncmp(rcv_cmd, "decr cal res ", 13) == 0) {
                int index = atoi(rcv_cmd + 13);
                int cal_R;

                esp_err_t ret = config_comp_decr_calibration_resistance_offset(index - 1);
                ret = ret == ESP_OK ? config_comp_get_calibration_resistance_offset(index - 1, &cal_R) : ret;
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to decrement calibration resistance offset at index %d.\nError: %s", index, esp_err_to_name(ret));
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":%s}", esp_err_to_name(ret));
                } else {
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"index\":%d, \"cal_R\":%d}", index, cal_R);
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                }
                
            } else if (strncmp(rcv_cmd, "set cal res ", 12) == 0) {
                char *args_ptr = rcv_cmd + 12;
                int index;
                int cal_R;
                int items_scanned = sscanf(args_ptr, "%d %d", &index, &cal_R);

                if (items_scanned == 2) {
                    // Command expects 1-based index, function takes 0-based.
                    // (e.g., user types "set cal res 1 100", index is 1, we pass 0 to function)
                    int fetched_cal_R;
                    esp_err_t ret_set = config_comp_set_calibration_resistance_offset(index - 1, cal_R);
                    esp_err_t ret_get = ESP_FAIL; 

                    if (ret_set == ESP_OK) {
                        ret_get = config_comp_get_calibration_resistance_offset(index - 1, &fetched_cal_R);
                    }

                    esp_err_t final_ret = (ret_set == ESP_OK) ? ret_get : ret_set;

                    if (final_ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set/get calibration resistance offset for index %d to %d. Error: %s", index, cal_R, esp_err_to_name(final_ret));
                        snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":\"%s\"}", esp_err_to_name(final_ret));
                    } else {
                        snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"index\":%d, \"cal_R\":%d}", index, fetched_cal_R);
                    }
                } else {
                    ESP_LOGE(TAG, "Malformed 'set cal res' command: '%s'. Expected: set cal res <index> <value>", rcv_cmd);
                    snprintf(s_serial_buffer, SERIAL_BUFFER_SIZE, "{\"error\":\"malformed command syntax for set cal res\"}");
                }
                esp_err_t send_ret = serial_comp_send(s_serial_buffer);
                if (send_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send command processing result over serial.\nError: %s", esp_err_to_name(send_ret));;
                }
                
            } else {
                ESP_LOGW(TAG, "Unknown command received: '%s'", rcv_cmd);
            }

        } else {
            if (config_comp_get_serial_stream_active()) { // xQueueReceive timed out / do periodic tasks
                _get_and_send_latest_temps_json(s_serial_buffer, SERIAL_BUFFER_SIZE);
                // if (config_comp_get_log_temps_active()) {
                //     ESP_LOGI("", "%s", s_serial_buffer);
                // }                
            }
        }
    }
}