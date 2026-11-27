# MQTT topics and message contract

Default device ID: `device01`

| Topic | Publisher | Subscriber | Purpose |
|---|---|---|---|
| `fallband/device01/sensor` | ESP8266 | Node.js backend | Periodic sensor telemetry (nominally every 200 ms while MQTT is connected) |
| `fallband/device01/events` | ESP8266 | Node.js backend | Fall event when the firmware enters the alarm state |
| `fallband/device01/status` | ESP8266 / MQTT last will | Node.js backend | Retained online/offline status |
| `fallband/device01/command` | Node.js backend | ESP8266 | Remote commands such as `reset_alarm` |

## Sensor payload

The firmware publishes JSON fields including `device_id`, `accel_x`, `accel_y`, `accel_z`, `acceleration_magnitude`, `gyro_x`, `gyro_y`, `gyro_z`, `gyro_magnitude_dps`, `tilt`, `fall_detected` and `buzzer`.

## Fall event payload

The firmware publishes `device_id`, `event_type` (`fall_detected`), `acceleration_magnitude`, `gyro_magnitude_dps` and `tilt`. The supplied firmware event payload does not currently include a timestamp or event ID.

## Status payload

The firmware publishes a retained online status after connecting. Its MQTT last-will message publishes an offline status if the broker detects an unexpected disconnect.

## Reset command

The backend publishes `{"command":"reset_alarm"}` to the command topic. The firmware subscribes to that topic and resets its local state when it receives the command.
