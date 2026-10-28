# Telemetry packet format

The transmitter serializes a packed `ImuPkt` structure directly into the radio payload. The receiver checks its length, header bytes, and application CRC before decoding it.

| Byte offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| 0 | 1 | `sync` | `0xAA` |
| 1 | 1 | `type` | `0x01` |
| 2 | 1 | `seq` | Unsigned sequence counter; wraps after 255 |
| 3 | 2 | `lin_x` | `linear.x × 1000`, saturated to `int16_t` |
| 5 | 2 | `lin_y` | `linear.y × 1000`, saturated to `int16_t` |
| 7 | 2 | `lin_z` | `linear.z × 1000`, saturated to `int16_t` |
| 9 | 2 | `ang_x` | `angular.x × 1000`, saturated to `int16_t` |
| 11 | 2 | `ang_y` | `angular.y × 1000`, saturated to `int16_t` |
| 13 | 2 | `ang_z` | `angular.z × 1000`, saturated to `int16_t` |
| 15 | 1 | `crc` | CRC-8 over bytes 0–14 |

Total payload: **16 bytes**. The ESP32 endpoints use little-endian integers. The original implementation relies on their native byte order rather than explicitly encoding a platform-independent wire format.

## Quantization

For each field, the transmitter multiplies by 1000, clamps to −32768 through 32767, then converts to `int16_t`. Conversion truncates toward zero. The receiver divides by `1000.0f`.

This gives nominal 0.001-unit steps and a range of **−32.768 through +32.767**. The scale is applied identically to all six fields. In the project's telemetry convention, `angular.z` holds yaw in degrees, so yaw outside this limited range saturates rather than retaining a complete turn.

## Integrity check

The application CRC uses polynomial **0x07**, initial value **0x00**, no reflection, and no final XOR. It covers all packet bytes except the final CRC byte. Its check value for ASCII `123456789` is `0xF4`.

This CRC is distinct from the radio's PHY CRC. It detects corruption; it provides neither encryption nor sender authentication. The frame includes no timestamp or acknowledgement, and the receiver does not currently calculate packet-loss statistics from its sequence counter.

## Host validation

`tools/validate_packet.py` extracts the packet, CRC, and quantizer definitions from the actual sketches and compiles a small host harness with `g++`. It checks transmitter/receiver layout compatibility, CRC vectors, encoded values, numeric bounds, and corrupted payloads. The test scope is the binary protocol, not the Arduino hardware drivers or micro-ROS connection.
