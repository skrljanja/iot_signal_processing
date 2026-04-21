# iot_signal_processing
Internet of Things individual assignment

## Description

The goal of the assignment is to create an IoT system that collects information from a sensor, analyses the data locally and communicates to a nearby server an aggregated value of the sensor readings. The IoT system adapts the sampling frequency in order to save energy and reduce communication overhead. The IoT device will be based on an ESP32 prototype board and the firmware will be developed using the FreeRTOS. You are free to use IoT-Lab or real devices.

## Input signal 

We are free to choose any signal of the form SUM(a_k*sin(f_k)). I have chosen: 

```
input_signal(t) = 5*sin(2*pi*2*t)+11*sin(2*pi*12*t)
```
as defined in src/main-sampler.cpp

## Maximum Sampling Frequency
Note, that we are not using a real sensor, so really the maximum frequency depends on the task clock - since I am using vTaskDelay(1), which means there is 1ms delay between taking samples, the maximum frequency should be 1000Hz. 

I also look at the actual sampling rate experimentally, by counting samples in the loop.

If we wanted to test the actual feasible maximum we can use yield() instead.  This way, the actual sampling frequency goes up to rouhgly 42kHz. However, this might stop other tasks so it is an unsafe way of sampling, if we want the same board to also do other things. 

## Identify optimal sampling frequency
This is performed using the FFT. 
For robustness, I perform the FFT 6 times and take the average of the maximum frequencies found.

## Compute aggregate function over a window
TaskAggregation calculates the average over a window 

## Communicating the aggregate value
* Communication to the edge: Using MQTT over Wifi
* Communication to the cloud: Using LoRaWAN+TTN - since the sampling and aggregating and FFT and communication is all done by the same board, connecting to LoRA overwhelmed the board, compromising the FFT transformations. Therefore, this code has been commented out. 

Note: when on Wokwi, use public broker for MQTT (uncomment and comment appropriate code in main-sampler.cpp) AND a virtual WiFI network called Wokwi-GUEST.

## Measure the performance of the system
### Energy 
Since the sending is dependent on time, as opposed to number of samples, the result is actually really similar for both scenarios. 

### Per window execution 
Both take around 12-13ms, with oversampling taking order of magnitude 0.1ms more on average. 

### Volume of Data

### End-to-end Latency 
d
## Bonus: Other signals

## Setup

To connect to WiFi and MQTT broker, follow these steps:  
 * 1. Set up your WiFi credentials (SSID and password) in the code (Both your PC and ESP32 should be connected to the same WiFi network)
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