#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;
const int mySDA = 4; 
const int mySCL = 5;

void setup(void)
{
  Serial.begin(115200);
  // Manually start the Wire library with my pins
  Wire.begin(mySDA, mySCL);

  while (!Serial) {
      delay(1);
  }
  
  // Initialize the INA219 
   if (! ina219.begin()) {
    Serial.println("Failed. Chip not found. Check wiring.");
    while (1) { delay(10); }
  }
  // Set calibration for INA219 to measure up to 16V and 400mA
  ina219.setCalibration_16V_400mA();
  Serial.println("Measuring energy consumption with INA219:");
}

void loop(void)
{
  float current_mA = 0;
  current_mA = ina219.getCurrent_mA();
  Serial.println(current_mA);
  delay(50);
}
