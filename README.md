# iot_signal_processing
Internet of Things individual assignment

## Assigment

The goal of the assignment is to create an IoT system that collects information from a sensor, analyses the data locally and communicates to a nearby server an aggregated value of the sensor readings. The IoT system adapts the sampling frequency in order to save energy and reduce communication overhead. The IoT device will be based on an ESP32 prototype board and the firmware will be developed using the FreeRTOS. You are free to use IoT-Lab or real devices.

## Input signal 

We are free to choose any signal of the form SUM(a_k*sin(f_k)). I have chosen: 

```
input_signal(t) = 3*sin(2*pi*t)+2*sin(2*pi*7*t)
```


/*
 * ESP32 2 Cores Signal Processing System
 * - DAC Signal Generation + Data Processing
 * - ADC Sampling
 * 
 * To connect to WiFi and MQTT broker, follow these steps:  
 * 1. Set up your WiFi credentials (SSID and password) in the code.
 *  -   Both your PC and ESP32 should be connected to the same WiFi network.
 * 2. Set up your MQTT broker address, config and port. 
 *  - C:\Program Files\mosquitto\mosquitto.conf should have the following uncomented lines:
 *      - listener 1883
 *      - allow_anonymous true
 *  - mqtt_server = Use Win + r, open command prompt using cmd, and type ipconfig to find your local IP address 192.168.XXX.XX
 *  - mqtt_port = 1883; This is default port for MQTT. You could change it in the file mosquitto.conf
 *  - mqtt_topic = This is the topic for publishing data. Can be any name you want.
 * 3. To start the MQTT broker open a terminal and paste: mosquitto -c "C:\Program Files\mosquitto\mosquitto.conf" -v .
 *  - netstat -ano | findstr :1883  to show the port is open 
 * 

 * 4. Open another terminal and paste: mosquitto_sub -h <mqtt_server> -t "<mqtt_topic>" -v to subscribe to the topic.
 * 
 * 5. Upload the code to your ESP32 board.
 * 6. Open the Serial Monitor to see the output.
 */