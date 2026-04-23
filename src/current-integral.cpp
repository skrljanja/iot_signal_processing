#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;
const int mySDA = 4; 
const int mySCL = 5;

// Variables for integration
float total_mAs = 0;           // Accumulated milliamp-seconds
unsigned long last_time = 0;
unsigned long start_time = 0;
bool finished = false;

void setup(void) {
  Serial.begin(115200);
  Wire.begin(mySDA, mySCL);

  if (!ina219.begin()) {
    Serial.println("Failed. Chip not found.");
    while (1) { delay(10); }
  }
  
  ina219.setCalibration_16V_400mA();
  Serial.println("Starting 30-second measurement...");
  
  start_time = millis();
  last_time = start_time;
}

void loop(void) {
  unsigned long current_time = millis();
  unsigned long elapsed = current_time - start_time;

  if (elapsed <= 30000) { // Run for 30,000ms (30 seconds)
    float current_mA = ina219.getCurrent_mA();
    
    // Calculate time since last reading in seconds
    float delta_t = (current_time - last_time) / 1000.0;
    
    // Accumulate Charge: Current * Time
    total_mAs += current_mA * delta_t;
    
    last_time = current_time;

    Serial.print("Current: "); Serial.print(current_mA); Serial.print(" mA | ");
    Serial.print("Total so far: "); Serial.print(total_mAs); Serial.println(" mAs");
    
    delay(100); // 10Hz sampling is usually plenty for steady loads
  } 
  else if (!finished) {
    // Final Calculation
    float total_mAh = total_mAs / 3600.0;
    
    Serial.println("--- Results ---");
    Serial.print("Total Charge (mAs): "); Serial.println(total_mAs);
    Serial.print("Total Capacity (mAh): "); Serial.println(total_mAh, 6);
    finished = true;
  }
}