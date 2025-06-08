#pragma once

#include "esp_err.h"
#include "driver/usb_serial_jtag.h" // Needed for the full USB JTAG driver
//#include "driver/usb_serial_jtag_vfs.h"

#define SERIAL_BUFFER_SIZE 2048
#define SERIAL_STACK_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the serial component (if needed)
esp_err_t serial_comp_init(void);

// // Sends a null-terminated string over serial
// void serial_comp_send_string(const char* str);

// // Reads a line of text from serial, blocks until a line is received or buffer is full
// // Returns the number of characters read (the length of the incoming buffer, excluding null terminator), or -1 on error.
// int serial_comp_read_line(char* buffer, int max_len);


/**
 * @brief Reads data from the serial component into the provided buffer.
 *
 * This function attempts to read data from the serial interface and stores it
 * into the buffer provided by the caller.
 *
 * @param buffer Pointer to a character array where the read data will be stored.
 *               The buffer must be allocated by the caller and should be large
 *               enough to hold the expected data.
 *
 * @return The number of bytes read on success, or a negative value on error.
 */
int serial_comp_receive(char *buffer, int max_len);


/**
 * @brief Sends a string over the serial interface.
 *
 * This function transmits the specified null-terminated string via the serial communication component.
 *
 * @param[in] str Pointer to the null-terminated string to be sent.
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_INVALID_ARG_t on NULL or empty input argument
 */
esp_err_t serial_comp_send(const char* str);


/**
 * @brief Task function for handling serial communication.
 *
 * This function implements the main loop for the serial communication component.
 * It is intended to be run as a FreeRTOS task.
 *
 * @param arg Pointer to user-defined data passed to the task (can be NULL).
 */
void serial_comp_task(void *arg);


/**
 * @brief Task function for handling serial reception.
 *
 * This function implements the main loop for receiving serial commands.
 * It is intended to be run as a FreeRTOS task.
 *
 * @param arg Pointer to user-defined data passed to the task (can be NULL).
 */
void serial_rx_task(void *arg); // Declare if created by main, or keep static if created by serial_comp_init


#ifdef __cplusplus
}
#endif