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
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();
  power_mW = ina219.getPower_mW();
  loadvoltage = busvoltage + (shuntvoltage / 1000);

  // print tab seperated value for serial monitor graphing
  Serial.print(busvoltage);
  Serial.print("\t");
  Serial.print(shuntvoltage);
  Serial.print("\t");
  Serial.print(loadvoltage);
  Serial.print("\t");
  Serial.print(current_mA);
  Serial.print("\t");
  Serial.println(power_mW);

  delay(50);
}
