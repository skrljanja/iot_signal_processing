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
* Communication to the cloud: Using LoRaWAN+TTN - the connection is established only once we have already performed the FFT and adapted the sampling rate. 

Note: when on Wokwi, use public broker for MQTT (uncomment and comment appropriate code in main-sampler.cpp) AND a virtual WiFI network called Wokwi-GUEST.

## Measure the performance of the system
### Energy 
Wiring 2 ESP32s + an INA 219 board, where 1 board is performing the sampling, communication and aggregation, and the other is reading the power and current readings given by the INA board. This is what the circuit looks like:

![Circuit](img/circuit.jpeg)

Since the sending is dependent on time, as opposed to number of samples, the result is actually really similar for both scenarios. 


![mA current using adaptive sampling](img/adaptive_sampling.png)

![mA current using oversampling](img/oversampled.png)

Here, I get an unexpacted result: the power consumption is actually really similar. 
In order to try and see a difference I calibrated the INA resolution, but it did not change the results. 

The possible reasons for the lack of difference are:
* The power supply is an USB connection to my laptop: the USB-to-UART bridge chip comsumes power and adds noise to the base current, masking small differences caused by frequency
* The communication is the power bottleneck, also masking the small difference caused by sampling 

### Per window execution 
This was measured by noting the time at the start of the aggregation, and printing the time elapsed once we have sent the aggregated data via WiFi. 

Both take around 14ms (including MQTT sending and LoRa sending), with oversampling taking order of magnitude 0.1ms more on average. The aggregation is likely a much smaller part of the execution time compared to the MQTT communication. 

### Volume of Data
Measured using Wireshark. Install Wireshark and navigate to the directory. Then run (as admin):

```
tshark -i [INDEX] -f "host [IP] and port 1883" -a duration:30 -w output.pcap
```

to find volume of data in 30 seconds. In 30 seconds we expect to send an aggregate 6 times (since we aggregate every 5 seconds).
Regardless of sampling rates, the payload size is the same (12 packets). The lack of difference is expected. 

## Bonus: Other signals
To try other signals, uncomment the different amplitude/frequency variables in main-sampler.cpp (20-35).

As with the sampling the difference was not large, neither is it at other signals. 
The FFT gets close to the theoretical ideal frequency, only as long as the max frequency is bounded. 

When the frequency of one of the waves is too high, the found max frequency actually corresponds to the frequency of the other lower-frequency wave.

```
input_signal(t) = 4*sin(2*pi*7*t)+11*sin(2*pi*100*t)
```

## LLM Analysis 
In contrast to my approach, the LLM only does it using 2 tasks. 
Despite being given whole instructions, it does not produce code for connecting to WiFi and LoRa, but produces mostly correct code for the sampling, the FFT transform and the sampling noise filters. The communication could also be done if I employed further prompting. 
However, for the FFT it only finds the peak frequency, not the maximum represented frequency, which is incorrect for adjusting sampling frequency - could lead for undersampling. 
Other observations
* The LLM adds a 10% safety margin to the Nyquist Frequency.
* It uses mutexes, which are potentially overkill for this project, but are good practice 

## Setup
Clone this repository. I am running it in VSCode, using the platformIO and WOKWI plugins (Wokwi is only necessary in case that you want to run it on a simulated chip).
The dependencies are listed in the .ini file and will resolve when building the project using platformIO.

Monitoring the power consumption is not possible in the simulation - 2 ESP32 chips are required, one for running the code, the other for monitoring power consumption. 

To connect to WiFi, edit the WiFi configuration variables in main-sampler.cpp (96-100)