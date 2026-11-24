#pragma once

#include <stdint.h>
#include <string.h>
#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual const char* getSettingValue(int i) const { return NULL; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }
  virtual bool isGPSDetected() const { return false; }

  // Optional DHT11 temperature/humidity sensor on a runtime-selectable digital pin.
  // Default implementation: not supported (the UI hides the related menu items).
  virtual bool supportsDht11() const { return false; }
  virtual void configureDht11(bool enabled, int8_t pin) { (void)enabled; (void)pin; }
  virtual bool getDht11(float& temperature, float& humidity) { (void)temperature; (void)humidity; return false; }

  // Optional DS18B20 (1-Wire) temperature sensor(s) on a runtime-selectable digital pin.
  // Several sensors may share the bus; idx = 0 .. getDs18b20Count()-1.
  // Default implementation: not supported (the UI hides the related menu items).
  virtual bool supportsDs18b20() const { return false; }
  virtual void configureDs18b20(bool enabled, int8_t pin) { (void)enabled; (void)pin; }
  virtual uint8_t getDs18b20Count() const { return 0; }
  virtual bool getDs18b20(uint8_t idx, float& temperature) { (void)idx; (void)temperature; return false; }

  // Environment readings WITHOUT the GPIO sensors above (DHT11 / DS18B20) and without GPS.
  // The UI lists DHT11 / DS18B20 by name, the rest (I2C sensors) from this LPP buffer.
  virtual bool queryOnboardSensors(CayenneLPP& telemetry) { return querySensors(TELEM_PERM_ENVIRONMENT, telemetry); }

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
