#ifndef GPIO_PIN_COUNT
#define GPIO_PIN_COUNT SOC_GPIO_PIN_COUNT
#endif

#include <Arduino.h>
#include <WiFi.h>          // WiFi library for ESP32
#include <PubSubClient.h>  // MQTT client library
#include <arduinoFFT.h>    // FFT processing library
#include "LoRaWan_APP.h"  // LoRaWAN library


#define FFT_SAMPLE_SIZE 128                 // FFT window size
#define QUEUE_LENGTH (FFT_SAMPLE_SIZE * 4)  // Size of FreeRTOS queues
#define DAC_PIN 25                          // GPIO25 used for DAC output
#define SINE_DEBUG false

const int offset = 128;            // DC offset for sine wave
// const int amplitude = 5;           // Amplitude of sine wave 1
// const float frequency = 2.0;       // Base frequency for DAC signal  1
// const int amplitude2 = 11;         // Amplitude of sine wave 2
// const float frequency2 = 12.0;     // Base frequency for DAC signal 2
// // BONUS SIGNAL 1
const int amplitude = 1;           // Amplitude of sine wave 1
const float frequency = 23.0;       // Base frequency for DAC signal  1
const int amplitude2 = 127;         // Amplitude of sine wave 2
const float frequency2 = 5.0;     // Base frequency for DAC signal 2
// // // BONUS SIGNAL 2
// const int amplitude = 9;           // Amplitude of sine wave 1
// const float frequency = 43.0;       // Base frequency for DAC signal  1
// const int amplitude2 = 6;         // Amplitude of sine wave 2
// const float frequency2 = 700.0;     // Base frequency for DAC signal 2

const int dacUpdateRate = 500;          // DAC update rate in Hz
volatile float sampleFrequency = 1000.0;  // Initial maximun ADC sampling frequency
volatile float currentAvg = 0.0;          // Current rolling average


/* TTN KEYS (LSB for EUIs, MSB for AppKey) */
uint8_t devEui[] = { 0xA6, 0xA8, 0xD3, 0x19, 0x0F, 0x16, 0x34, 0xA1 };
uint8_t appEui[] = { 0xCA, 0xE1, 0xD7, 0xC5, 0x20, 0xF6, 0x40, 0xFC };
uint8_t appKey[] = {0x85, 0xEB, 0x71, 0xDC, 0x79, 0xE7, 0xCF, 0x29, 0x28, 0x51, 0xDF, 0xE2, 0x1B, 0xFD, 0x95, 0x3A}; // Use the actual AppKey value here

/* LoRaWAN Configuration */
uint32_t  license[4] = {0xD5397DF0, 0x8573F814, 0x7A38C73D, 0x48D68363}; // V3 often doesn't need this, but good to have
DeviceClass_t  loraWanClass = CLASS_A;
LoRaMacRegion_t loraWanRegion = LORAMAC_REGION_EU868;
bool overTheAirActivation = true;
bool adaptiveDr = true;
bool isTxConfirmed = true;
uint8_t appPort = 2;

// The channels mask for your region (EU868 uses 0001)
uint16_t userChannelsMask[6] = { 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
// Number of trials for a 'confirmed' message
uint8_t confirmedNbTrials = 4;
// ADR (Adaptive Data Rate) - set to true for battery efficiency
bool loraWanAdr = true;
// Duty cycle of the application (15 seconds)
uint32_t appTxDutyCycle = 15000;
// These are required by the linker even for OTAA mode
uint32_t devAddr = (uint32_t)0;
uint8_t nwkSKey[] = { 0 };
uint8_t appSKey[] = { 0 };

// Global variables
QueueHandle_t sampleQueue;               // Queue for FFT samples
QueueHandle_t aggQueue;                  // Queue for averaging
TaskHandle_t ADC_TaskHandle = NULL;      // Handle for ADC task
TaskHandle_t Process_TaskHandle = NULL;  // Handle for processing task
bool loraConnected = false;

// Struct for ADC data and its timing
typedef struct {
  int adc_value;
  unsigned long delta_time;
} ADCData_t;


// MQTT CONFIGURATION

volatile bool mqtt_connected = false;  // MQTT connection status flag
// const char* ssid = "Anja's Galaxy A32 5G";
// const char* password = "marina123";
const char* ssid = "H6745-94508588";
const char* password = "XK3eHhyFzC";
// For PHYSICAL BOARD uncomment this and use local broker
//const char* mqtt_server = "10.154.173.32"; // local IP address
// for WOKWI SIMULATION use the public broker
//const char *mqtt_server = "broker.hivemq.com";//"broker.emqx.io";//Public Broker
const char *mqtt_server = "broker.emqx.io";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;  
const char* mqtt_topic = "artificial-signal";  // MQTT topic for publishing

WiFiClient espClient;            // WiFi client for MQTT
PubSubClient client(espClient);  // MQTT client object

portMUX_TYPE samplingMux = portMUX_INITIALIZER_UNLOCKED;

RTC_DATA_ATTR bool fftPerformed = false;            // Flag to track FFT
RTC_DATA_ATTR float savedSampleFrequency = 1000.0;    // Default sampling frequency


void connectToWiFi() {
  Serial.println("{\"wifi_status\":\"Connecting to WiFi...\"}");
  WiFi.begin(ssid, password);
  // for WOKWI simulation
  //WiFi.mode(WIFI_STA);
  //WiFi.begin("Wokwi-GUEST", NULL);

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 10000;  // 10 seconds timeout

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("{\"wifi_status\":\"Connected!\"}");
  } else {
    Serial.println("{\"wifi_status\":\"Failed to connect to WiFi. Restarting...\"}");
    // ESP.restart();  // Uncomment if you want to restart the ESP32 on failure
  }
}

void connectToMQTT() {
  Serial.println("{\"mqtt_status\":\"Connecting to MQTT broker...\"}");
  int attempts = 0;
  const int maxAttempts = 2;  // Maximum number of connection attempts

  while (!client.connected() && attempts < maxAttempts) {
    Serial.printf("{\"mqtt_status\":\"Attempt %d/%d...\"}\n", attempts + 1, maxAttempts);

    if (client.connect("Sampler_ESP32")) {  
      Serial.println("{\"mqtt_status\":\"Connected to broker!\"}");
      return;
    } else {
      Serial.printf("{\"mqtt_status\":\"Connection failed, rc=%d. Retrying in 2 seconds...\"}\n", client.state());
      delay(2000);
      attempts++;
    }
  }

  if (!client.connected()) {
    if (client.subscribe(mqtt_topic)) {
      Serial.println("{\"mqtt_status\":\"Subscribed to topic!\"}");
    }
    Serial.println("{\"mqtt_status\":\"Failed to connect to MQTT broker after multiple attempts. Restarting...\"}");
    // ESP.restart();  // Uncomment if you want to restart the ESP32 on failure
  }
}

void connectToLoRaWAN() {
  Serial.println("{\"lorawan_status\":\"Joining LoRaWAN network...\"}");
  Mcu.begin(0, 0); // Required for Heltec V3 power management
  
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);

  if (overTheAirActivation) {
    LoRaWAN.init(loraWanClass, loraWanRegion);
  }
  LoRaWAN.join(); // Start the join process
  Serial.println("{\"lorawan_status\":\"Join process initiated. Check display for status.\"}");
  loraConnected = true; // Set to true to allow sending data, but actual join status should be checked in the main loop or via event callbacks
}

// Sine lookup table - this is faster then calling sin() in the DAC task. 
#define TABLE_SIZE 512
int sineTable1[TABLE_SIZE];
int sineTable2[TABLE_SIZE];

// Call this once in void setup(). This makes the sampling faster then calling sin() everytime.
void initSineTable() {
    for (int i = 0; i < TABLE_SIZE; i++) {
        // Pre-calculate: (amplitude * sin(angle) + offset)
        // Adjust values to fit 8-bit DAC (0-255)
        float angle = (2.0 * PI * i) / TABLE_SIZE;
        sineTable1[i] = (int)(amplitude * sin(angle * frequency) + offset); // Combine two sine waves
        sineTable2[i] = (int)(amplitude2 * sin(frequency2 * angle)); // Second sine wave
    }
}

// DAC Signal generated - core 1
void TaskDACWrite(void* pvParameters) {
    // Instead of phase in radians, we track the "index" in the table
    float index1 = 0;
    float index2 = (4.0 / 3.0) * (TABLE_SIZE / 2.0); // Phase offset
    unsigned long loopCount = 0;
    unsigned long benchmarkTimer = millis();
    
    // Increment is based on: (Desired Freq * Table Size) / Sample Rate
    const float indexIncrement1 = (float)frequency * TABLE_SIZE / dacUpdateRate;
    const float indexIncrement2 = (float)frequency2 * TABLE_SIZE / dacUpdateRate;

    unsigned long lastUpdate = micros();
    const unsigned long interval = 1000000UL / dacUpdateRate;

    while (1) {
        unsigned long now = micros();
        
        if (now - lastUpdate >= interval) {
            // Fast lookup instead of sin()
            int val1 = sineTable1[(int)index1 % TABLE_SIZE];
            int val2 = sineTable2[(int)index2 % TABLE_SIZE];
            
            int sineSum = val1 + val2;
            
            // Constrain to 8-bit range to prevent wrapping/clipping
            if (sineSum > 255) sineSum = 255;
            if (sineSum < 0) sineSum = 0;

            ledcWrite(0, (uint8_t)sineSum);

            // Update indices
            index1 += indexIncrement1;
            if (index1 >= TABLE_SIZE) index1 -= TABLE_SIZE;
            
            index2 += indexIncrement2;
            if (index2 >= TABLE_SIZE) index2 -= TABLE_SIZE;

            lastUpdate = now;
        }
        loopCount++;
      // Use this code to find actual sampling rate.
      // Can comment after finding
      // if (millis() - benchmarkTimer >= 1000) {
      //   Serial.printf("Actual Sampling Rate: %lu Hz\n", loopCount);
      //   loopCount = 0;
      //   benchmarkTimer = millis();
      // } 
        
        // yield() allows high-frequency looping without the 1ms OS block.
        // yield(); 
        vTaskDelay(1);  // Use a small delay to allow other tasks to run, but not too long to miss DAC updates
    }
}

// NOTE: No ADC, this is simulated!
void TaskADCRead(void* parameter) {
  ADCData_t adc_data;
  uint8_t table_index = 0; // Wraps around automatically at 256 if uint8_t
  unsigned long lastSampleTime = micros();
  unsigned long t_prev = micros();

  while (1) {
    unsigned long samplePeriod = 1000000UL / savedSampleFrequency;
    unsigned long t_now = micros();

    if ((t_now - lastSampleTime) >= samplePeriod) {
      lastSampleTime += samplePeriod;
      adc_data.adc_value = ledcRead(0);
      adc_data.delta_time = t_now - t_prev;
      t_prev = t_now;

      int simulated_value2 = 128 + 1 * sin(2 * PI * 23 * (t_now / 1000000.0)) + 127 * sin(2 * PI * 5 * (t_now / 1000000.0));
      int simulated_value3 = 128 + 9 * sin(2 * PI * 43 * (t_now / 1000000.0)) + 6 * sin(2 * PI * 700 * (t_now / 1000000.0));

      // to plot samples, print this
      // Serial.println(String(adc_data.adc_value) + "\t" + String(simulated_value2) + "\t" + String(simulated_value3));
      xQueueSend(sampleQueue, &adc_data, 0);
      xQueueSend(aggQueue, &adc_data, 0);
    }
    vTaskDelay(1);
  }
}

// Data processing and FFT task (Core 1)
void TaskProcess(void* pvParameters) {
  if (fftPerformed) {
    Serial.println("[FFT] Skipping FFT computation (already performed).");
    vTaskDelete(NULL);  // Terminate the task
    connectToLoRaWAN();  // Connect to LoRaWAN network even if FFT is skipped
    return;
  }

  ADCData_t data;
  float vReal[FFT_SAMPLE_SIZE];
  float vImag[FFT_SAMPLE_SIZE];
  float freqs[FFT_SAMPLE_SIZE];
  int count = 0;
  float prev_maxFreq = 0.0;
  float maxFreq = 0.0;
  float totalSampleFrequency = 0.0;

  while (count <= 5) {
    float sum = 0.0;
    float maxFreq = 0.0;

    // Receive FFT_SAMPLE_SIZE samples
    for (int i = 0; i < FFT_SAMPLE_SIZE; i++) {
      if (xQueueReceive(sampleQueue, &data, portMAX_DELAY) == pdTRUE) {
        freqs[i] = 1e6 / data.delta_time;  // Calculate sampling frequency from delta time
        vReal[i] = data.adc_value;         // Real part of FFT input
        vImag[i] = 0.0;                    // Imaginary part initialized to 0
        sum += freqs[i];                   // Accumulate sampling frequencies
        //Serial.println("{\"adc_value\":" + String(data.adc_value) + ",\"delta_time\":" + String(data.delta_time) + ",\"calculated_freq\":" + String(freqs[i]) + "}");
        // Serial.println(String(data.adc_value));  // For plotting raw ADC values
      } else {
        Serial.print("Process Error: Queue broken!");
        break;
      }
    }

    float mean_sampling_freq = sum / FFT_SAMPLE_SIZE;

    ArduinoFFT<float> FFT(vReal, vImag, FFT_SAMPLE_SIZE, mean_sampling_freq, false);

    // Track ADC min and max values
    int min_adc = 1024, max_adc = 0;
    for (int i = 0; i < FFT_SAMPLE_SIZE; i++) {
      if (vReal[i] < min_adc) min_adc = vReal[i];
      if (vReal[i] > max_adc) max_adc = vReal[i];
    }

    FFT.windowing(vReal, FFT_SAMPLE_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);  // Apply window
    FFT.compute(vReal, vImag, FFT_SAMPLE_SIZE, FFT_FORWARD);                  // Compute FFT
    FFT.complexToMagnitude(vReal, vImag, FFT_SAMPLE_SIZE);  // Convert to magnitude

    for (int i = 0; i < 20; i++) vReal[i] = 0.0;  // Zero out low-frequency bins

    float peakFrequency = FFT.majorPeak(vReal, FFT_SAMPLE_SIZE, mean_sampling_freq);

    float binWidth = mean_sampling_freq / FFT_SAMPLE_SIZE;

    int N = FFT_SAMPLE_SIZE / 2;

    float sum2 = 0.0;
    for (int i = 1; i < N; i++) sum2 += vReal[i];
    float mean = sum2 / (N - 1);

    float sq_diff = 0.0;
    for (int i = 1; i < N; i++) {
      float diff = vReal[i] - mean;
      sq_diff += diff * diff;
    }
    float stddev = sqrt(sq_diff / (N - 1));

    float threshold = mean + 2 * stddev;

    for (int i = 1; i < N; i++) {
      if (vReal[i] > threshold) {
        maxFreq = i * binWidth;
      }
    }
    //Serial.println(String(data.adc_value) + "\t" + String(maxFreq));

    if (maxFreq > 1.0 && maxFreq <= 10000.0) {
      portENTER_CRITICAL(&samplingMux);
      totalSampleFrequency += 2.2 * maxFreq;  // Adapt sampling frequency
      portEXIT_CRITICAL(&samplingMux);
    }

    count++;

    Serial.printf("[FFT] Max freq: %.2f Hz\n", maxFreq);
    Serial.printf("[FFT] FINAL Sample freq: %.2f Hz\n", sampleFrequency);
    Serial.printf("[FTT] Dominant Frequency is %.2f Hz \n", peakFrequency);
    Serial.printf("{\"sample_freq\":%.2f}\n", savedSampleFrequency);  // Add sampling frequency output

    vTaskDelay(pdMS_TO_TICKS(1000));  // Delay for 1000 milliseconds
  }

  if (count == 6) {
    portENTER_CRITICAL(&samplingMux);
    sampleFrequency = totalSampleFrequency / 5;  // Average sampling frequency
    savedSampleFrequency = sampleFrequency;  // Save to RTC memory
    portEXIT_CRITICAL(&samplingMux);
    connectToLoRaWAN();  // Connect to LoRaWAN network
}

  // uncomment to oversample at maximum frequency
  //sampleFrequency = 1000;
  //savedSampleFrequency = 1000;
  fftPerformed = true;  // Set the flag to indicate FFT has been performed
  Serial.println("[FFT] FFT computation completed. Flag set. The sampling frequency is adapted to ");
  Serial.printf("%.2f Hz\n", sampleFrequency);
  vTaskDelete(NULL);
}

// Aggregation task and publishing - core 2
void TaskAggregation(void* param) {
  unsigned long start_time = micros();
  unsigned long elapsed_time = 0;
  long sum = 0;
  int count = 0;
  ADCData_t data;

  while (1) {
    if (xQueueReceive(aggQueue, &data, portMAX_DELAY) == pdTRUE) {
      sum += data.adc_value;
      count++;
      elapsed_time = micros() - start_time;

      if (elapsed_time >= 5000000UL) {
        unsigned long aggregation_start = micros();  // Every 5 seconds
        float average = (count > 0) ? (float)sum / count : 0.0;
        unsigned long timestamp = micros();  // Capture the timestamp when data is sent
        currentAvg = average;  // Update the current average
        if (mqtt_connected) {
          char payload[64];
          snprintf(payload, sizeof(payload), "{\"average\":%.2f,\"timestamp\":%lu}", average, timestamp);
          if (client.publish(mqtt_topic, payload)) {
            Serial.printf("{\"mqtt_status\":\"Sent average: %.2f\"}\n", average);
          } else {
            // Serial.println("{\"mqtt_status\":\"Failed to send data!\"}");
          }
        }

        // Reset counters
        start_time = micros();
        sum = 0;
        count = 0;
        Serial.printf("Per window execution time: ");
        Serial.printf("%lu ms\n", (micros() - aggregation_start) / 1000);
      }
    }
  }
}

void TaskLoRaSend(void* param) {
  while (1) {
    if (loraConnected) {
      if (fftPerformed) {
      float averageToSend = currentAvg;  // Capture the current average for sending
      memcpy(appData, &averageToSend, 4);
      appDataSize = 4; 
      // Check if the radio is busy before sending
      LoRaWAN.send();
      Serial.println("[LoRa] Packet queued for transmission");
    }
    vTaskDelay(5000);  // Check every 5500 ms
  } // Handle LoRaWAN events
    }
  
}


unsigned long lastSleepTime = 0;  // Tracks the last time deep sleep was triggered
void printIntArray(int arr[], int size) {
  Serial.println("--- Array Contents ---");
  for (int i = 0; i < size; i++) {
    Serial.print(arr[i]);
    
    // Add a comma and space between numbers
    if (i < size - 1) {
      Serial.print(", ");
    }

    // Every 16 elements, start a new line for readability
    if ((i + 1) % 16 == 0) {
      Serial.println();
    }
  }
  Serial.println("\n--- End of Array ---");
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(DAC_PIN, 0);
  initSineTable();

  // Check if the ESP32 woke up from deep sleep
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("{\"status\":\"Woke up from deep sleep\"}");
  } else {
    Serial.println("{\"status\":\"Power-on or reset\"}");
    fftPerformed = false;  // Reset the flag on power-on
    sampleFrequency = 50.0;  // Default sampling frequency
    savedSampleFrequency = sampleFrequency;  // Reset saved frequency
  }

  connectToWiFi();  // Connect to WiFi
  client.setServer(mqtt_server, mqtt_port);
  connectToMQTT();                      // Connect to MQTT broker
  mqtt_connected = client.connected();  // Set connection flag

  // Send WiFi and MQTT status
  Serial.printf(
    "{\"wifi_status\":\"%s\",\"wifi_ip\":\"%s\",\"mqtt_status\":\"%s\"}\n",
    (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected",
    WiFi.localIP().toString().c_str(),
    mqtt_connected ? "connected" : "disconnected"
  );

  ledcWrite(0, offset);       // Initialize DAC output
  analogReadResolution(8);         // 8-bit ADC resolution
  analogSetAttenuation(ADC_11db);  // ADC attenuation setting

  sampleQueue = xQueueCreate(QUEUE_LENGTH, sizeof(ADCData_t));
  if (sampleQueue == NULL) {
    Serial.print("FATAL: Queue creation failed!");
    while (1)
      ;
  }

  aggQueue = xQueueCreate(QUEUE_LENGTH, sizeof(ADCData_t));
  if (aggQueue == NULL) {
    Serial.print("FATAL: Queue creation failed!");
    while (1)
      ;
  }

  // Create tasks and assign them to specific cores
  xTaskCreatePinnedToCore(TaskDACWrite, "DAC_Gen", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskADCRead, "ADC_Sample", 4096, NULL, 2, &ADC_TaskHandle, 1);
  xTaskCreatePinnedToCore(TaskProcess, "Data_Process", 8192, NULL, 1, &Process_TaskHandle, 0);
  xTaskCreatePinnedToCore(TaskAggregation, "RollingAverage", 4096, NULL, 2, NULL, 1);
  Serial.print("System Initialized: Tasks Running");

  // Set up deep sleep for 10 seconds
  esp_sleep_enable_timer_wakeup(10 * 1000000);  // 10 seconds in microseconds
}

// Main loop is a fallback, mostly unused
void loop() {
  unsigned long currentTime = millis();

  // Check if 1 minute has passed since the last deep sleep
  if (currentTime - lastSleepTime >= 60000) {  // 60,000 ms = 1 minute
    Serial.println("{\"status\":\"Entering deep sleep for 10 seconds\"}");
    delay(200);  // Allow time for the message to be sent
    lastSleepTime = currentTime;  // Update the last sleep time
    esp_deep_sleep_start();       // Enter deep sleep
  }
  delay(1000);  // Add a delay to avoid busy looping
}
