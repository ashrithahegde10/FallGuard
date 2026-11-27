#pragma once

// Copy this file to secrets.h and replace every placeholder.
// Use a 2.4 GHz Wi-Fi network for ESP8266 compatibility.

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// IP address of the machine running Mosquitto, reachable from the ESP8266.
const char* MQTT_SERVER = "192.168.1.100";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";

const char* DEVICE_ID = "device01";
const char* SENSOR_TOPIC = "fallband/device01/sensor";
const char* EVENT_TOPIC = "fallband/device01/events";
const char* STATUS_TOPIC = "fallband/device01/status";
const char* COMMAND_TOPIC = "fallband/device01/command";
