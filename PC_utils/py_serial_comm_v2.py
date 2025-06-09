
import serial
import json
import threading
import time
import datetime
import csv
from collections import deque
import matplotlib.pyplot as plt
import os

# --- Configuration ---
ESP_SERIAL_PORT = "COM8"  # <<<<<<< IMPORTANT: Use correct ESP32-C6 COM port
BAUD_RATE = 115200
max_log_size = 1000 # Maximum number of data points to keep in memory

# --- Shared Data ---
# Using a deque for efficient appends and optionally for capping size
sensor_data_log = deque(maxlen=max_log_size)
config_log = deque(maxlen=max_log_size)
data_lock = threading.Lock()
stop_event = threading.Event()
g_serial_instance = None
datadir = "sensor_data"
sampling_interval = 1000

def serial_reader_thread_func(port, baudrate, data_list, config_list, lock, stop_event_flag):
    """
    Thread function to read serial data, parse JSON, and append to a shared list.
    """
    ser = None
    global g_serial_instance
    while not stop_event_flag.is_set():
        try:
            if ser is None or not ser.is_open:
                print(f"Attempting to connect to {port} at {baudrate} baud...")
                # For ESP32 native USB, DTR/RTS handling might be important for resets or bootloader
                # For general communication after boot, it might not be strictly needed.
                ser = serial.Serial(port=None, baudrate=baudrate, timeout=1) # Open later
                ser.port = port
                # ser.dtr = False # Data Terminal Ready - uncomment if connection issues
                # ser.rts = False # Request To Send - uncomment if connection issues
                ser.open()
                g_serial_instance = ser
                print(f"Connected to {ser.name}")
                time.sleep(0.1) # Small delay after opening

            if ser.in_waiting > 0:
                line_bytes = ser.readline()
                if line_bytes:
                    try:
                        line_str = line_bytes.decode('utf-8').strip()
                        if line_str: # Ensure it's not an empty line after strip
                            data_point = json.loads(line_str)
                            with lock:
                                if "names" in data_point and "temperatures" in data_point:
                                    data_point['timestamp_ms'] = time.time_ns() // 1_000_000
                                    data_list.append(data_point)
                                else:
                                    config_point = {
                                        "timestamp_ms": time.time_ns() // 1_000_000,
                                        "config_point": data_point
                                    }
                                    if "sampling_interval_ms" in data_point:
                                        global sampling_interval
                                        sampling_interval = data_point["sampling_interval_ms"]
                                        print(f"Updated sampling interval to {sampling_interval} ms")
                                    config_list.append(config_point) 
                            # print(f"Logged: {data_point}") # Uncomment for verbose logging
                    except json.JSONDecodeError as e:
                        # print(f"JSON Decode Error: {e} - Received: '{line_str}'")
                        # print(f"Received non-JSON: '{line_str}'")
                        print(line_str) #type: ignore
                    except UnicodeDecodeError as e:
                        print(f"Unicode Decode Error: {e} - Received bytes: {line_bytes}")
                    except Exception as e:
                        print(f"An unexpected error occurred while processing data: {e}")
            else:
                # No data, sleep briefly to avoid busy-waiting if timeout is short or None
                time.sleep(0.01)

        except serial.SerialException as e:
            print(f"Serial Error: {e}. Reconnecting in 5 seconds...")
            if ser and ser.is_open:
                ser.close()
            g_serial_instance = None
            ser = None # Reset ser object to trigger re-open attempt
            time.sleep(5)
        except Exception as e:
            print(f"An unexpected error occurred in reader thread: {e}")
            # Potentially add a delay here too before retrying
            time.sleep(1)


    if ser and ser.is_open:
        ser.close()
    g_serial_instance = None
    print("Serial reader thread stopped.")

def clear_data_log(data_list, lock):
    """Clears the sensor data log."""
    with lock:
        data_list.clear()
    print("Sensor data log cleared.")

def clear_config_log(config_list, lock):
    """Clears the config changes log."""
    with lock:
        config_list.clear()
    print("Config log cleared.")

def save_data_log_csv(data_list, config_list, lock):
    """Saves the sensor data log and config info log to a CSV file with a timestamp."""
    if not data_list:
        print("No data to save.")
        return

    # Copy data to a local list while holding the lock
    with lock:
        data_to_save = list(data_list)
        config_to_save = list(config_list)

    timestamp_str = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    
    if not os.path.exists(datadir):
        try:
            os.makedirs(datadir)
            print(f"Created directory: {datadir}")
        except OSError as e:
            print(f"Error creating directory {datadir}: {e}")
            return # Cannot save if directory creation fails
    
    filename = os.path.join(datadir, f"sensor_data_{timestamp_str}.csv")
    filename_cfg = os.path.join(datadir, f"config_data_{timestamp_str}.csv")

    try:
        with open(filename, 'w', newline='', encoding='utf-8') as csvfile:
            fieldnames = ['timestamp_ms', 'names', 'temperatures']
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

            writer.writeheader()
            for data_point in data_to_save:
                # Ensure all expected keys exist, provide default if not
                row = {key: data_point.get(key, None) for key in fieldnames}
                writer.writerow(row)

        print(f"Data saved to {filename}")

    except IOError as e:
        print(f"Error saving file {filename}: {e}")
    
    with open(filename_cfg, 'w', newline='', encoding='utf-8') as csvfile:
        fieldnames = ['timestamp_ms', 'config_point']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

        writer.writeheader()
        for config_point in config_to_save:
            row = {key: config_point.get(key, None) for key in fieldnames}
            writer.writerow(row)


def plot_data_log(data_list, lock):
    """Plots the sensor data log."""
    if not data_list:
        print("No data to plot.")
        return

    # Copy data to a local list while holding the lock
    with lock:
        data_to_plot = list(data_list)

    names = data_to_plot[0].get('names', [])

    # Extract data
    timestamps_ms = [d.get('timestamp_ms', 0) for d in data_to_plot]
    # names = [d.get('names', []) for d in data_to_plot]
    temps = [d.get('temperatures', []) for d in data_to_plot]

    # Convert timestamps to seconds relative to the first timestamp
    if timestamps_ms:
        start_time_ms = timestamps_ms[0]
        relative_timestamps_sec = [(ts - start_time_ms) / 1000.0 for ts in timestamps_ms]
    else:
        relative_timestamps_sec = []

    # Plotting logic moved inside the function
    if relative_timestamps_sec:
        
        plt.figure(figsize=(10, 6))
        # temps_by_sensor = list(zip(*temps))
        # for sensor_temps, name in zip(temps_by_sensor, names):
        #     plt.plot(relative_timestamps_sec, sensor_temps, label=name)
        plt.plot(relative_timestamps_sec, temps, label = names)
        plt.xlabel("Time (seconds relative to start)")
        plt.ylabel("Temperatures (C)")
        plt.title("Temperature vs Time")
        plt.legend()
        
        plt.grid(True)

        # # Plot 2: Temperatures vs Time
        # plt.figure(figsize=(10, 6))
        # plt.plot(relative_timestamps_sec, temps, label="External Temp")
        # plt.plot(relative_timestamps_sec, internal_temps, label="Internal Temp")
        # plt.xlabel("Time (seconds relative to start)")
        # plt.ylabel("Temperature (°C)")
        # plt.title("Temperature vs Time")
        # plt.legend()
        # plt.grid(True)

        plt.show() # This will block until plot windows are closed
    else:
        print("No data points to plot.")

    # Import matplotlib here to avoid needing it if plotting is never used
    # import matplotlib.pyplot as plt

def main():
    global max_log_size, sensor_data_log, config_log
    print("Starting ESP32 Data Logger...")
    print(f"Logging data to a list with max size: {max_log_size}")

    # Start the serial reader thread
    reader_thread = threading.Thread(
        target=serial_reader_thread_func,
        args=(ESP_SERIAL_PORT, BAUD_RATE, sensor_data_log, config_log, data_lock, stop_event),
        daemon=True # Daemon threads exit when the main program exits
    )
    reader_thread.start()

    print("Reader thread started. Press Ctrl+C to stop.")
    time.sleep(1)

    try:
        while True:
            
            print(f"\nEnter command: \
                  \n(c)lear data log, (cc)lear config log, (r)efresh to show prompt, (s)ave data and config, (g)raph, (q)uit, (d)ata latest \
                  \nTo change the 'max_log_size' use `size <new_size>` (currently max_log_size = {max_log_size} -> {sampling_interval/1000 * max_log_size} seconds for current sampling interval of {sampling_interval/1000}s.)\
                  \nTo send a remote command use 'cmd <device_recognisable_cmd>'")
            command = input("> ").strip().lower()

            if command == 'c':
                clear_data_log(sensor_data_log, data_lock)
            elif command == 'cc':
                clear_config_log(config_log, data_lock)
            elif command == 's':
                save_data_log_csv(sensor_data_log, config_log, data_lock)
            elif command == 'g':
                plot_data_log(sensor_data_log, data_lock)
            elif command == 'q':
                break # Exit the loop
            elif command == 'r':
                continue
            elif command == 'd':
                 with data_lock:
                    current_log_size = len(sensor_data_log)
                    latest_entry = sensor_data_log[-1] if current_log_size > 0 else "N/A"
                    print(f"\n--- Log Status --- Logged data points: {current_log_size}, Latest entry: {latest_entry}")
            elif command.startswith("size "):
                try:
                    new_size = int(command[5:].strip())
                    if new_size <= 0 or new_size > 1_000_000:
                        print("Data log size must be an integer, larger than 0, and less than 1,000,000.")
                    else:
                        max_log_size = new_size
                        sensor_data_log = deque(maxlen=max_log_size)
                        config_log = deque(maxlen=max_log_size)
                        print(f"Data log size set to {max_log_size}. This will keep ~{sampling_interval/1000 * max_log_size} seconds for current sampling interval of {sampling_interval/1000}s.")
                except ValueError:
                    print("Invalid data size value entry.")
            
            elif command.startswith("cmd "):
                actual_cmd = command[4:].strip() # Extract command after "cmd "
                if not actual_cmd:
                    print("Command cannot be empty.")
                elif g_serial_instance and g_serial_instance.is_open:
                    try:
                        g_serial_instance.write(actual_cmd.encode('utf-8') + b'\n') # Send command with newline
                        print(f"Sent command: '{actual_cmd}'. Response will appear in the stream if ESP32 replies.")
                        if "status" not in actual_cmd and "get temps" not in actual_cmd: 
                            time.sleep(1)
                    except Exception as e:
                        print(f"Error sending command: {e}")
                else:
                    print("Serial port not connected or command interface not ready.")
            else:
                print(f"\n--- Log Status ({time.strftime('%H:%M:%S')}) ---")
                print("No data logged yet.")

    except KeyboardInterrupt:
        print("\nStopping application...")
    finally:
        stop_event.set()
        if reader_thread.is_alive():
            print("Waiting for reader thread to finish...")
            reader_thread.join(timeout=5) # Wait for the thread to close
        print("Application stopped.")

if __name__ == "__main__":
    main()
