#include "serial_comp.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "config_comp.h"
#include "temp_comp.h"

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
        } else {
            ESP_LOGD(TAG, "Successfully sent temperature JSON.");
        }
    } else if (ret == ESP_OK) {
        // This case might occur if there are no active thermistors, resulting in an empty JSON object/array.
        ESP_LOGD(TAG, "Temperature JSON is empty, nothing to send.");
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
    char received_command_buffer[MAX_COMMAND_LEN];
    TickType_t queue_timeout_ticks;

    while(1) {
        queue_timeout_ticks = pdMS_TO_TICKS(config_comp_get_sampling_interval());

        if (xQueueReceive(s_command_queue, received_command_buffer, queue_timeout_ticks) == pdTRUE) { // cmd received
            ESP_LOGI(TAG, "Processing command: %s", received_command_buffer);
            // TODO: handle commands
        } else {
            if (config_comp_get_serial_stream_active()) { // xQueueReceive timed out / do periodic tasks
                _get_and_send_latest_temps_json(s_serial_buffer, SERIAL_BUFFER_SIZE);
            }
        }
    }
}