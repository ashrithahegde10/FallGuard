#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"  // Create this file from secrets.example.h; never commit real credentials.

// ==========================================
// PRIVATE NETWORK CONFIGURATION
// ==========================================
// Copy secrets.example.h to secrets.h and enter your own values.
// secrets.h is excluded from Git by the repository .gitignore.

WiFiClient espClient;
PubSubClient mqttClient(espClient);

Adafruit_MPU6050 mpu;

// ==========================================
// PIN CONFIGURATION
// ==========================================

#define BUZZER D5

// Active-LOW buzzer: LOW = ON, HIGH = OFF

// ==========================================
// FALL DETECTION SETTINGS
// ==========================================

const float FREE_FALL_G = 0.60;
const float IMPACT_G = 2.40;

const float ROTATION_DPS = 180.0;
const float TILT_THRESHOLD = 45.0;

const float STILL_G_MIN = 0.85;
const float STILL_G_MAX = 1.15;
const float STILL_GYRO_DPS = 20.0;

// Timings (milliseconds)
const unsigned long FREE_FALL_DURATION = 80;
const unsigned long IMPACT_WINDOW = 1500;
const unsigned long FALL_CHECK_WINDOW = 5000;
const unsigned long INACTIVITY_TIME = 1800;
const unsigned long ALARM_DURATION = 3000;

// ==========================================
// NETWORK TIMERS
// ==========================================

const unsigned long SENSOR_PUBLISH_INTERVAL = 200;
const unsigned long WIFI_RETRY_INTERVAL = 5000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;

unsigned long lastMqttPublish = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMqttAttempt = 0;

// ==========================================
// SYSTEM STATES
// ==========================================

enum State {
  NORMAL,
  WAITING_FOR_IMPACT,
  CHECKING_AFTER_IMPACT,
  ALARM
};

State state = NORMAL;

// ==========================================
// TIMERS
// ==========================================

unsigned long freeFallStart = 0;
unsigned long freeFallTime = 0;
unsigned long impactTime = 0;
unsigned long inactivityStart = 0;
unsigned long alarmStart = 0;
unsigned long lastPrintTime = 0;

// ==========================================
// CALIBRATION VARIABLES
// ==========================================

float baselineX = 0;
float baselineY = 0;
float baselineZ = 0;

float gyroOffsetX = 0;
float gyroOffsetY = 0;
float gyroOffsetZ = 0;

bool fallEvidenceDetected = false;
bool alarmActive = false;

// ==========================================
// FORWARD DECLARATIONS
// ==========================================

void resetSystem();
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload,
                  unsigned int length);
void publishSensorData(
  float ax, float ay, float az,
  float accelG, float gyroDPS, float tilt,
  float gx, float gy, float gz
);
void publishFallEvent(
  float accelG, float gyroDPS, float tilt
);

// ==========================================
// WIFI SCAN DIAGNOSTICS
// ==========================================

void scanWiFiNetworks() {
  Serial.println();
  Serial.println("Scanning nearby Wi-Fi networks...");

  int networkCount = WiFi.scanNetworks();

  if (networkCount == 0) {
    Serial.println("No Wi-Fi networks found.");
  } else {
    Serial.print("Networks found: ");
    Serial.println(networkCount);

    bool targetFound = false;

    for (int i = 0; i < networkCount; i++) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" | RSSI: ");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm");

      if (WiFi.SSID(i) == String(WIFI_SSID)) {
        targetFound = true;
        Serial.println(">>> TARGET HOTSPOT FOUND <<<");
      }
    }

    if (!targetFound) {
      Serial.println("WARNING: Target hotspot was NOT found.");
      Serial.println("Check hotspot name, 2.4 GHz band, and hotspot visibility.");
    }
  }

  WiFi.scanDelete();
  Serial.println("Wi-Fi scan complete.");
}

// ==========================================
// WIFI CONNECTION
// ==========================================

void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  if (now - lastWiFiAttempt < WIFI_RETRY_INTERVAL) {
    return;
  }

  lastWiFiAttempt = now;

  Serial.println();
  Serial.println("Connecting to Wi-Fi...");
  Serial.print("Target SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Current Wi-Fi status code: ");
  Serial.println(WiFi.status());

  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ==========================================
// MQTT CONNECTION
// ==========================================

void connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();

  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL) {
    return;
  }

  lastMqttAttempt = now;

  Serial.println("Connecting to MQTT broker...");

  bool connected = mqttClient.connect(
    "FallGuard_Device01",
    MQTT_USER,
    MQTT_PASSWORD,
    STATUS_TOPIC,
    1,
    true,
    "{\"device_id\":\"device01\",\"online\":false}"
  );

  if (connected) {

    Serial.println("MQTT CONNECTED!");

    mqttClient.subscribe(COMMAND_TOPIC);

    mqttClient.publish(
      STATUS_TOPIC,
      "{\"device_id\":\"device01\",\"online\":true}",
      true
    );

    Serial.println("Subscribed to command topic");

  } else {

    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqttClient.state());
    Serial.println("Check broker IP, port 1883, credentials, and laptop firewall.");
  }
}

// ==========================================
// MQTT COMMAND CALLBACK
// ==========================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {

  String receivedTopic = String(topic);
  String message;

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("MQTT command received: ");
  Serial.println(message);

  if (receivedTopic == COMMAND_TOPIC &&
      message.indexOf("reset_alarm") >= 0) {

    Serial.println("REMOTE RESET COMMAND");

    resetSystem();
  }
}

// ==========================================
// SENSOR DATA PUBLISHING
// ==========================================

void publishSensorData(
  float ax, float ay, float az,
  float accelG, float gyroDPS, float tilt,
  float gx, float gy, float gz
) {

  if (!mqttClient.connected()) {
    return;
  }

  char payload[384];

  snprintf(
    payload,
    sizeof(payload),

    "{\"device_id\":\"device01\","
    "\"accel_x\":%.3f,"
    "\"accel_y\":%.3f,"
    "\"accel_z\":%.3f,"
    "\"acceleration_magnitude\":%.3f,"
    "\"gyro_x\":%.2f,"
    "\"gyro_y\":%.2f,"
    "\"gyro_z\":%.2f,"
    "\"gyro_magnitude_dps\":%.2f,"
    "\"tilt\":%.2f,"
    "\"fall_detected\":%s,"
    "\"buzzer\":%s}",

    ax / 9.80665,
    ay / 9.80665,
    az / 9.80665,

    accelG,

    gx * 180.0 / PI,
    gy * 180.0 / PI,
    gz * 180.0 / PI,

    gyroDPS,
    tilt,

    state == ALARM ? "true" : "false",
    alarmActive ? "true" : "false"
  );

  bool published = mqttClient.publish(
    SENSOR_TOPIC,
    payload
  );

  if (published) {
    Serial.println("Sensor data published");
  } else {
    Serial.println("Sensor publish failed");
  }
}

// ==========================================
// FALL EVENT PUBLISHING
// ==========================================

void publishFallEvent(
  float accelG,
  float gyroDPS,
  float tilt
) {

  if (!mqttClient.connected()) {
    Serial.println("Fall event not published: MQTT offline");
    return;
  }

  char payload[256];

  snprintf(
    payload,
    sizeof(payload),

    "{\"device_id\":\"device01\","
    "\"event_type\":\"fall_detected\","
    "\"acceleration_magnitude\":%.3f,"
    "\"gyro_magnitude_dps\":%.2f,"
    "\"tilt\":%.2f}",

    accelG,
    gyroDPS,
    tilt
  );

  bool published = mqttClient.publish(
    EVENT_TOPIC,
    payload
  );

  if (published) {
    Serial.println("FALL EVENT PUBLISHED!");
  } else {
    Serial.println("Fall event publish failed");
  }
}

// ==========================================
// BUZZER CONTROL
// ==========================================

void buzzerOn() {

  digitalWrite(BUZZER, LOW);

  alarmStart = millis();
  alarmActive = true;
}

void buzzerOff() {

  digitalWrite(BUZZER, HIGH);

  alarmActive = false;
}

void updateBuzzer() {

  if (alarmActive &&
      millis() - alarmStart >= ALARM_DURATION) {

    buzzerOff();

    Serial.println("BUZZER STOPPED AFTER 3 SECONDS");
  }
}

// ==========================================
// SYSTEM RESET
// ==========================================

void resetSystem() {

  buzzerOff();

  state = NORMAL;

  freeFallStart = 0;
  freeFallTime = 0;
  impactTime = 0;
  inactivityStart = 0;

  fallEvidenceDetected = false;

  Serial.println("SYSTEM RESET");
}

// ==========================================
// ORIENTATION CALCULATION
// ==========================================

float angleBetween(
  float ax, float ay, float az,
  float bx, float by, float bz
) {

  float dot = ax * bx + ay * by + az * bz;

  float magA = sqrt(ax * ax + ay * ay + az * az);
  float magB = sqrt(bx * bx + by * by + bz * bz);

  if (magA == 0 || magB == 0) {
    return 0;
  }

  float cosine = dot / (magA * magB);

  cosine = constrain(cosine, -1.0, 1.0);

  return acos(cosine) * 180.0 / PI;
}

// ==========================================
// SENSOR CALIBRATION
// ==========================================

void calibrateSensor() {

  Serial.println("Keep sensor completely STILL...");

  delay(2000);

  sensors_event_t a, g, temp;

  float axSum = 0, aySum = 0, azSum = 0;
  float gxSum = 0, gySum = 0, gzSum = 0;

  const int samples = 500;

  for (int i = 0; i < samples; i++) {

    mpu.getEvent(&a, &g, &temp);

    axSum += a.acceleration.x;
    aySum += a.acceleration.y;
    azSum += a.acceleration.z;

    gxSum += g.gyro.x;
    gySum += g.gyro.y;
    gzSum += g.gyro.z;

    delay(5);
  }

  baselineX = axSum / samples;
  baselineY = aySum / samples;
  baselineZ = azSum / samples;

  gyroOffsetX = gxSum / samples;
  gyroOffsetY = gySum / samples;
  gyroOffsetZ = gzSum / samples;

  Serial.println("CALIBRATION COMPLETE");

  Serial.print("GX offset: ");
  Serial.println(gyroOffsetX * 180.0 / PI, 2);

  Serial.print("GY offset: ");
  Serial.println(gyroOffsetY * 180.0 / PI, 2);

  Serial.print("GZ offset: ");
  Serial.println(gyroOffsetZ * 180.0 / PI, 2);
}

// ==========================================
// SETUP
// ==========================================

void setup() {

  Serial.begin(115200);

  Wire.begin(D2, D1);

  pinMode(BUZZER, OUTPUT);

  buzzerOff();

  Serial.println();
  Serial.println("==============================");
  Serial.println("FALLGUARD SYSTEM");
  Serial.println("==============================");

  // Initialize MPU6050

  if (!mpu.begin()) {

    Serial.println("ERROR: MPU6050 NOT DETECTED");

    while (true) {
      buzzerOff();
      delay(1000);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  calibrateSensor();

  // Initialize Wi-Fi

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  scanWiFiNetworks();
  connectWiFi();

  // Initialize MQTT

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(2);
  mqttClient.setBufferSize(512);

  Serial.println("FALL DETECTION ACTIVE");
  Serial.println("BUZZER STATUS: OFF");
  Serial.println("MQTT CONNECTION PENDING");
  Serial.println("Send 'r' to reset alarm");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {

  unsigned long now = millis();

  // ========================================
  // NETWORK MANAGEMENT
  // ========================================

  connectWiFi();

  static bool wifiConnectedMessageShown = false;

  if (WiFi.status() == WL_CONNECTED && !wifiConnectedMessageShown) {
    wifiConnectedMessageShown = true;
    Serial.println();
    Serial.println("================================");
    Serial.println("WIFI CONNECTED!");
    Serial.print("ESP8266 IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MQTT broker address: ");
    Serial.println(MQTT_SERVER);
    Serial.println("================================");
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiConnectedMessageShown = false;
  }

  if (WiFi.status() == WL_CONNECTED) {

    connectMQTT();

    if (mqttClient.connected()) {
      mqttClient.loop();
    }
  }

  // ========================================
  // MANUAL SERIAL RESET
  // ========================================

  if (Serial.available()) {

    char command = Serial.read();

    if (command == 'r' || command == 'R') {
      resetSystem();
    }
  }

  // ========================================
  // BUZZER TIMER
  // ========================================

  updateBuzzer();

  // ========================================
  // READ MPU6050
  // ========================================

  sensors_event_t a, g, temp;

  mpu.getEvent(&a, &g, &temp);

  // Accelerometer readings

  float ax = a.acceleration.x;
  float ay = a.acceleration.y;
  float az = a.acceleration.z;

  float accelG = sqrt(
    ax * ax + ay * ay + az * az
  ) / 9.80665;

  // Corrected gyroscope readings

  float gx = g.gyro.x - gyroOffsetX;
  float gy = g.gyro.y - gyroOffsetY;
  float gz = g.gyro.z - gyroOffsetZ;

  float gyroDPS = sqrt(
    gx * gx + gy * gy + gz * gz
  ) * 180.0 / PI;

  // Tilt calculation

  float tilt = angleBetween(
    baselineX, baselineY, baselineZ,
    ax, ay, az
  );

  // ========================================
  // SERIAL MONITOR OUTPUT
  // ========================================

  if (now - lastPrintTime >= 200) {

    lastPrintTime = now;

    Serial.print("Accel: ");
    Serial.print(accelG, 2);

    Serial.print(" g | Gyro: ");
    Serial.print(gyroDPS, 1);

    Serial.print(" dps | Tilt: ");
    Serial.print(tilt, 1);

    Serial.print(" | State: ");

    switch (state) {

      case NORMAL:
        Serial.println("NORMAL");
        break;

      case WAITING_FOR_IMPACT:
        Serial.println("WAITING FOR IMPACT");
        break;

      case CHECKING_AFTER_IMPACT:
        Serial.println("CHECKING AFTER IMPACT");
        break;

      case ALARM:
        Serial.println("ALARM");
        break;
    }
  }

  // ========================================
  // FALL DETECTION STATE MACHINE
  // ========================================

  switch (state) {

    // STATE 1: NORMAL

    case NORMAL:

      if (accelG < FREE_FALL_G) {

        if (freeFallStart == 0) {
          freeFallStart = now;
        }

        if (now - freeFallStart >= FREE_FALL_DURATION) {

          freeFallTime = now;

          state = WAITING_FOR_IMPACT;

          Serial.println("FREE FALL DETECTED");
        }

      } else {

        freeFallStart = 0;
      }

      break;

    // STATE 2: WAITING FOR IMPACT

    case WAITING_FOR_IMPACT:

      if (accelG >= IMPACT_G &&
          now - freeFallTime <= IMPACT_WINDOW) {

        impactTime = now;
        inactivityStart = 0;

        fallEvidenceDetected = false;

        state = CHECKING_AFTER_IMPACT;

        Serial.println("IMPACT DETECTED");

      }

      else if (now - freeFallTime > IMPACT_WINDOW) {

        state = NORMAL;

        freeFallStart = 0;

        Serial.println("FALL SEQUENCE CANCELLED");
      }

      break;

    // STATE 3: CHECK AFTER IMPACT

    case CHECKING_AFTER_IMPACT: {

      if (tilt >= TILT_THRESHOLD ||
          gyroDPS >= ROTATION_DPS) {

        fallEvidenceDetected = true;
      }

      bool still =
        gyroDPS < STILL_GYRO_DPS &&
        accelG > STILL_G_MIN &&
        accelG < STILL_G_MAX;

      if (still) {

        if (inactivityStart == 0) {
          inactivityStart = now;
        }

      } else {

        inactivityStart = 0;
      }

      bool inactiveLongEnough =
        inactivityStart != 0 &&
        now - inactivityStart >= INACTIVITY_TIME;

      if (now - impactTime <= FALL_CHECK_WINDOW &&
          fallEvidenceDetected &&
          inactiveLongEnough) {

        state = ALARM;

        buzzerOn();

        Serial.println();
        Serial.println("==============================");
        Serial.println("POSSIBLE FALL DETECTED!");
        Serial.println("BUZZER ACTIVATED FOR 3 SECONDS");
        Serial.println("==============================");

        // Publish exactly one fall event on transition

        publishFallEvent(accelG, gyroDPS, tilt);

      }

      else if (now - impactTime > FALL_CHECK_WINDOW) {

        state = NORMAL;

        freeFallStart = 0;
        inactivityStart = 0;

        fallEvidenceDetected = false;

        Serial.println("FALL NOT CONFIRMED");
      }

      break;
    }

    // STATE 4: ALARM

    case ALARM:

      // Buzzer is stopped by updateBuzzer()

      if (!alarmActive) {

        Serial.println("ALARM COMPLETED");

        resetSystem();
      }

      break;
  }

  // ========================================
  // PERIODIC SENSOR PUBLISHING
  // ========================================

  now = millis();

  if (mqttClient.connected() &&
      now - lastMqttPublish >= SENSOR_PUBLISH_INTERVAL) {

    lastMqttPublish = now;

    publishSensorData(
      ax, ay, az,
      accelG, gyroDPS, tilt,
      gx, gy, gz
    );
  }

  // ========================================
  // LOOP DELAY
  // ========================================

  delay(10);
}