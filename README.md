| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# Thermistron

A simple sketch to monitor - up to - five thermistors and send the measurements to the host via serial (serial is channeled via JTAG).     
A python script is included here for communicating with the device, timestamping the measurements, and storing/interacting with the data.  

Timestamping happens on the host side, because otherwise WiFi connections would have to be initiated, NTP servers contacted etc.   

