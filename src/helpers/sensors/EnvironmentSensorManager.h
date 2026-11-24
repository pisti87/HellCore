#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/sensors/Dht11.h>
#include <helpers/sensors/Ds18b20.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  Dht11    _dht;
  Ds18b20  _ds18b20;

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  bool isGPSDetected() const override { return gps_detected; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;   // GPS / BSEC / DS18B20 (a DS18B20 állapotgépe miatt mindig)
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;

  // DHT11 (runtime configurable, see Dht11.h)
  bool supportsDht11() const override;
  void configureDht11(bool enabled, int8_t pin) override;
  bool getDht11(float& temperature, float& humidity) override;

  // DS18B20 (runtime configurable, see Ds18b20.h)
  bool supportsDs18b20() const override;
  void configureDs18b20(bool enabled, int8_t pin) override;
  uint8_t getDs18b20Count() const override;
  bool getDs18b20(uint8_t idx, float& temperature) override;

  // I2C environment sensors only (no GPS / DHT11 / DS18B20)
  bool queryOnboardSensors(CayenneLPP& telemetry) override;
};
