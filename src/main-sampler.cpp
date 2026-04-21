
#include <WiFi.h>          // WiFi library for ESP32
#include <PubSubClient.h>  // MQTT client library
#include <arduinoFFT.h>    // FFT processing library

// ======================
// CONFIGURATION CONSTANTS
// ======================
#define FFT_SAMPLE_SIZE 128                 // FFT window size
#define QUEUE_LENGTH (FFT_SAMPLE_SIZE * 4)  // Size of FreeRTOS queues
#define ADC_PIN 34                          // GPIO34 used for ADC input
#define DAC_PIN 25                          // GPIO25 used for DAC output
#define SINE_DEBUG false

const int offset = 128;            // DC offset for sine wave
const int amplitude = 5;           // Amplitude of sine wave1
const float frequency = 2.0;       // Base frequency for DAC signal  1
const int amplitude2 = 11;         // Amplitude of sine wave 2
const float frequency2 = 12.0;     // Base frequency for DAC signal 2

const int dacUpdateRate = 500;          // DAC update rate in Hz
volatile float sampleFrequency = 50.0;  // Initial maximun ADC sampling frequency

// ======================
// RTOS GLOBAL VARIABLES
// ======================
QueueHandle_t sampleQueue;               // Queue for FFT samples
QueueHandle_t aggQueue;                  // Queue for averaging
TaskHandle_t ADC_TaskHandle = NULL;      // Handle for ADC task
TaskHandle_t Process_TaskHandle = NULL;  // Handle for processing task

// Struct for ADC data and its timing
typedef struct {
  int adc_value;
  unsigned long delta_time;
} ADCData_t;


// MQTT CONFIGURATION

volatile bool mqtt_connected = false;  // MQTT connection status flag
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";
const int mqtt_port = 1883;               // MQTT port
const char* mqtt_topic = "artificial-signal";  // MQTT topic for publishing

WiFiClient espClient;            // WiFi client for MQTT
PubSubClient client(espClient);  // MQTT client object

portMUX_TYPE samplingMux = portMUX_INITIALIZER_UNLOCKED;

RTC_DATA_ATTR bool fftPerformed = false;            // Flag to track FFT
RTC_DATA_ATTR float savedSampleFrequency = 50.0;    // Default sampling frequency

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------


// ======================
// WIFI CONNECTION HELPER
// ======================
void connectToWiFi() {
  Serial.println("{\"wifi_status\":\"Connecting to WiFi...\"}");
  WiFi.begin(ssid, password);

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

// ======================
// MQTT CONNECTION HELPER
// ======================
void connectToMQTT() {
  Serial.println("{\"mqtt_status\":\"Connecting to MQTT broker...\"}");
  int attempts = 0;
  const int maxAttempts = 5;  // Maximum number of connection attempts

  while (!client.connected() && attempts < maxAttempts) {
    Serial.printf("{\"mqtt_status\":\"Attempt %d/%d...\"}\n", attempts + 1, maxAttempts);

    if (client.connect("ESP32Client")) {  // Replace "ESP32Client" with a unique client ID if needed
      Serial.println("{\"mqtt_status\":\"Connected to broker!\"}");
      return;
    } else {
      Serial.printf("{\"mqtt_status\":\"Connection failed, rc=%d. Retrying in 2 seconds...\"}\n", client.state());
      delay(2000);
      attempts++;
    }
  }

  if (!client.connected()) {
    Serial.println("{\"mqtt_status\":\"Failed to connect to MQTT broker after multiple attempts. Restarting...\"}");
    // ESP.restart();  // Uncomment if you want to restart the ESP32 on failure
  }
}
// ======================
// DAC SIGNAL GENERATOR (Core 0)
// ======================
void TaskDACWrite(void* pvParameters) {
  float phase = 0;
  float phase2 = 4 * PI / 3;  // Phase offset for second sine wave
  const float phaseIncrement = 2 * PI * frequency / dacUpdateRate;
  const float phaseIncrement2 = 2 * PI * frequency2 / dacUpdateRate;
  unsigned long lastUpdate = micros();

  while (1) {
    unsigned long now = micros();
    if (now - lastUpdate >= (1000000UL / dacUpdateRate)) {
      int sineValue = (int)(amplitude * sin(phase) + offset);  // Generate sine wave value
      int sineValue2 = (int)(amplitude * sin(phase2));         // Generate sine wave value
      int sineSum = sineValue + sineValue2;
      dacWrite(DAC_PIN, sineSum);  // Output to DAC
      phase += phaseIncrement;
      if (phase >= 2 * PI) phase -= 2 * PI;  // Keep phase within 0 to 2π
      phase2 += phaseIncrement2;
      if (phase2 >= 2 * PI) phase2 -= 2 * PI;  // Keep phase within 0 to 2π
      lastUpdate = now;

      // ------ PLOT SIGNAL TO DEBUG (SHOULD BE COMMENTED)--------
      if (SINE_DEBUG == true) {  
        Serial.printf("DATA:%lu,%d\n", micros(), sineSum);
      }
    }
    vTaskDelay(1);  // Yield to other tasks
  }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

// ======================
// ADC SAMPLING TASK (Core 1)
// ======================
void TaskADCRead(void* parameter) {
  ADCData_t adc_data;
  unsigned long t_prev = micros();  // Initial timestamp
  portENTER_CRITICAL(&samplingMux);
  unsigned long samplePeriod;
  portEXIT_CRITICAL(&samplingMux);
  unsigned long lastSampleTime = micros();
  while (1) {
    samplePeriod = 1000000UL / savedSampleFrequency;
    if ((micros() - lastSampleTime) >= samplePeriod) {
      lastSampleTime += samplePeriod;
      unsigned long t_now = micros();            // Capture time
      adc_data.adc_value = analogRead(ADC_PIN);  // Read ADC value
      adc_data.delta_time = t_now - t_prev;      // Time since last sample

      // Send sample to both queues (non-blocking)
      xQueueSend(sampleQueue, &adc_data, 0);
      xQueueSend(aggQueue, &adc_data, 0);

      t_prev = t_now;  // Update timestamp
    }
    vTaskDelay(1);
  }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------


// ======================
// DATA PROCESSING TASK (Core 0)
// ======================
void TaskProcess(void* pvParameters) {
  if (fftPerformed) {
    Serial.println("[FFT] Skipping FFT computation (already performed).");
    vTaskDelete(NULL);  // Terminate the task
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

    // Receive FFT_SAMPLE_SIZE samples
    for (int i = 0; i < FFT_SAMPLE_SIZE; i++) {
      if (xQueueReceive(sampleQueue, &data, portMAX_DELAY) == pdTRUE) {
        freqs[i] = 1e6 / data.delta_time;  // Calculate sampling frequency from delta time
        vReal[i] = data.adc_value;         // Real part of FFT input
        vImag[i] = 0.0;                    // Imaginary part initialized to 0
        sum += freqs[i];                   // Accumulate sampling frequencies
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

    for (int i = 0; i < 20; i++) vReal[i] = 0.0;  // Zero out low-frequency bins

    FFT.complexToMagnitude(vReal, vImag, FFT_SAMPLE_SIZE);  // Convert to magnitude

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

    if (maxFreq > 1.0 && maxFreq <= 10000.0) {
      portENTER_CRITICAL(&samplingMux);
      totalSampleFrequency += 2.0 * maxFreq;  // Adapt sampling frequency
      portEXIT_CRITICAL(&samplingMux);
    }

    // if (toPlot_fft) {
    //   for (int i = 1; i < N; i++) {
    //     float freq = i * binWidth;
    //     Serial.printf("FFT:%.2f:%.2f\n", freq, vReal[i]);  // FFT:<frequency_in_Hz>:<magnitude>
    //   }
    // }
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
}

  fftPerformed = true;  // Set the flag to indicate FFT has been performed
  Serial.println("[FFT] FFT computation completed. Flag set.");
  vTaskDelete(NULL);
}



// ======================
// AGGREGATION + MQTT PUBLISH TASK (Core 0)
// ======================
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

      if (elapsed_time >= 5000000UL) {  // Every 5 seconds
        float average = (count > 0) ? (float)sum / count : 0.0;
        unsigned long timestamp = micros();  // Capture the timestamp when data is sent

        // Send data with timestamp
        Serial.printf("{\"average\":%.2f,\"samples\":%d,\"timestamp\":%lu}\n", average, count, timestamp);

        Serial.printf("{\"average\":%.2f,\"samples\":%d}\n", average, count);
        Serial.printf("{\"sample_freq\":%.2f}\n", savedSampleFrequency);  // Add sampling frequency output

        if (mqtt_connected) {
          char payload[64];
          snprintf(payload, sizeof(payload), "{\"average\":%.2f,\"timestamp\":%lu}", average, timestamp);
          if (client.publish(mqtt_topic, payload)) {
            Serial.printf("{\"mqtt_status\":\"Sent average: %.2f\"}\n", average);
          } else {
            Serial.println("{\"mqtt_status\":\"Failed to send data!\"}");
          }
        }
        // Reset counters
        start_time = micros();
        sum = 0;
        count = 0;
      }
    }
  }
}

// ======================
// INITIALIZATION
// ======================
unsigned long lastSleepTime = 0;  // Tracks the last time deep sleep was triggered

void setup() {
  Serial.begin(115200);
  delay(1000);

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

  dacWrite(DAC_PIN, offset);       // Initialize DAC output
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
  xTaskCreatePinnedToCore(TaskAggregation, "RollingAverage", 4096, NULL, 1, NULL, 0);

  Serial.print("System Initialized: Tasks Running");

  // Set up deep sleep for 10 seconds
  esp_sleep_enable_timer_wakeup(10 * 1000000);  // 10 seconds in microseconds
}

// ======================
// MAIN LOOP (Unused)
// ======================
void loop() {
  unsigned long currentTime = millis();

  // Check if 1 minute has passed since the last deep sleep
  if (currentTime - lastSleepTime >= 60000) {  // 60,000 ms = 1 minute
    Serial.println("{\"status\":\"Entering deep sleep for 10 seconds\"}");
    delay(200);  // Allow time for the message to be sent
    lastSleepTime = currentTime;  // Update the last sleep time
    esp_deep_sleep_start();       // Enter deep sleep
  }

  // Perform other tasks here (e.g., sampling, MQTT communication)
  delay(1000);  // Add a delay to avoid busy looping
}
