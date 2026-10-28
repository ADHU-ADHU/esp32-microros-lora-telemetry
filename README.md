# ESP32 ROS 2 Telemetry over Wi-Fi and LoRa

Embedded sensor integration with **ROS 2 Humble**, **micro-ROS**, and **ESP32**. An ESP32 publishes DS18B20 temperature readings and subscribes to robot telemetry over Wi-Fi. A second ESP32 forwards robot telemetry through an SX1278 LoRa radio to a Heltec receiver, extending the system with a compact wireless telemetry link.

**Stack:** Arduino C++ · ESP32 · ROS 2 Humble · micro-ROS · DS18B20 · SX1278 · SX1262

[Firmware](firmware/) · [Hardware guide](docs/hardware.md) · [Setup and wiring](docs/setup.md) · [Packet format](docs/packet-format.md) · [Implementation notes](docs/implementation-notes.md)

## Hardware at a glance

<table>
  <tr>
    <th>ESP32 controller</th>
    <th>Temperature sensor</th>
  </tr>
  <tr>
    <td align="center"><a href="https://www.adafruit.com/product/3405"><img src="https://cdn-shop.adafruit.com/970x728/3405-08.jpg" height="180" alt="Adafruit HUZZAH32 ESP32 Feather reference board"></a><br><strong>Adafruit HUZZAH32 Feather</strong><br>ESP32 + 2.4 GHz Wi-Fi + USB serial</td>
    <td align="center"><a href="https://www.adafruit.com/product/381"><img src="https://cdn-shop.adafruit.com/970x728/381-01.jpg" height="180" alt="DS18B20 digital temperature probe reference equivalent"></a><br><strong>DS18B20 probe</strong><br>1-Wire temperature sensing on GPIO4</td>
  </tr>
  <tr>
    <th>LoRa transmitter radio</th>
    <th>LoRa receiver board</th>
  </tr>
  <tr>
    <td align="center"><a href="docs/hardware.md#sx1278-transmitter"><img src="docs/assets/sx1278-module.png" height="180" alt="SX1278 LoRa 433 MHz V4.0 module pictured in the project source material"></a><br><strong>SX1278 module</strong><br>External SPI radio + spring antenna</td>
    <td align="center"><a href="https://heltec.org/project/wireless-stick-lite-v2/"><img src="https://heltec.org/wp-content/uploads/2023/09/wireless-stickLite-front.png" height="180" alt="Heltec Wireless Stick Lite V3 product-family reference"></a><br><strong>Heltec Wireless Stick family</strong><br>ESP32-S3 + onboard SX1262 radio</td>
  </tr>
</table>

*Select a picture for its product page or hardware notes. Adafruit and Heltec pictures are manufacturer references; the SX1278 picture is extracted from the project material. The DS18B20 image is an equivalent probe, and the linked Heltec product identifies the Lite V3 family. These are component references, not photographs of the complete assembled setup. [Image credits and exact hardware notes](docs/hardware.md).*

| Component | Role in this system | Connection or setting |
| --- | --- | --- |
| Robot / Raspberry Pi 4 | Publishes robot telemetry | `/robot/imu` as `geometry_msgs/msg/Twist` |
| ROS 2 computer | Runs the micro-ROS agent and ROS graph | Humble, domain `10`, UDP port `8888` by default |
| ESP32 + DS18B20 | Publishes temperature and receives robot data | 2.4 GHz Wi-Fi; 1-Wire data on GPIO4 |
| ESP32 + SX1278 | Converts robot data to radio packets | SPI; CS `27`, RESET `33`, DIO0 `15` |
| Heltec + SX1262 | Validates and decodes received packets | LoRa receive; serial output at `115200` baud |

**Radio-band note:** the sketches configure **433 MHz**. The exact Heltec RF variant is not established by the product name; the report's product photo is marked EU868. Use a radio front end and antenna matched to the configured band. See the [hardware guide](docs/hardware.md#receiver-variant-and-radio-band) for the distinction.

## What the project does

- Publishes temperature as `/esp32/temperature` using `std_msgs/msg/Float32`.
- Subscribes to `/robot/imu` using `geometry_msgs/msg/Twist` through a micro-ROS agent.
- Packs six telemetry values into a **16-byte radio frame**, with a sequence number and CRC-8.
- Receives and validates the frame on a separate Heltec device, then prints decoded values to serial.

The core integration demonstrates ROS topic publishing and subscribing directly from microcontrollers. The LoRa extension adds a one-way radio hop from the Wi-Fi-connected transmitter to a separate serial receiver.

## System architecture

[![ESP32 transmission flow showing ROS topics, 2.4 GHz Wi-Fi, 433 MHz LoRa settings, packet format, and serial output](docs/assets/transmission-flow.svg)](docs/assets/transmission-flow.svg)

1. The robot publishes `/robot/imu` into the ROS 2 graph. The micro-ROS agent carries it over **Wi-Fi / UDP** to both ESP32 clients.
2. The sensor ESP32 publishes `/esp32/temperature` back through the agent. This temperature branch stays on Wi-Fi.
3. The transmitter ESP32 scales six `Twist` fields into signed 16-bit values, adds a sequence number and CRC-8, and sends the resulting **16-byte** packet over LoRa.
4. The Heltec receiver checks length, header, and CRC, decodes the values, and prints them with radio diagnostics to the serial monitor at **115200 baud**.

The LoRa hop carries **robot telemetry**, not the temperature topic. Fresh robot samples still reach the transmitter through Wi-Fi. The receiver is a serial endpoint and does not republish into ROS 2.

### Frequencies, rates, and packet settings

| Item | Configured value | What it means |
| --- | --- | --- |
| ESP32 Wi-Fi | 2.4 GHz band | IP connection to the micro-ROS agent; the channel is not fixed in the sketch |
| LoRa RF carrier | **433 MHz** | Direct transmitter-to-receiver radio link |
| LoRa modulation | **SF7 · 125 kHz bandwidth · coding rate 4/5** | Both radio endpoints must use matching settings |
| LoRa framing | Preamble `8`, public sync `0x34`, PHY CRC enabled | Radio-level framing and error checking |
| Application packet | **16 bytes**, sequence counter, six `int16_t` fields, CRC-8 | Compact encoding checked again by the receiver |
| Send schedule | At least **100 ms** between attempts; **at most 10 attempts/s** | Can repeat the last sample; this is not a measured fresh-data delivery rate |
| Serial monitor | **115200 baud** | USB/UART logging rate, separate from the radio carrier and send schedule |

The sensor publishes periodically. Its conversion and loop timing do not establish an exact fixed publication rate. [Full packet specification](docs/packet-format.md) · [Radio setup](docs/setup.md#radio-settings).

## Firmware

| Sketch | Hardware | Function |
| --- | --- | --- |
| [temperature_node](firmware/temperature_node/temperature_node.ino) | ESP32 and DS18B20 | Periodic temperature publication and robot IMU-topic subscription |
| [sx1278_imu_transmitter](firmware/sx1278_imu_transmitter/sx1278_imu_transmitter.ino) | ESP32 and SX1278 | Subscribe to robot telemetry, quantize values, build binary frames, and transmit over LoRa |
| [heltec_imu_receiver](firmware/heltec_imu_receiver/heltec_imu_receiver.ino) | Compatible Heltec ESP32-S3/SX1262 board | Check payload length, header, and CRC; decode values and print them to serial |

Each folder is a separate Arduino sketch. The SX1278 transmitter and Heltec receiver use different radio libraries; select the matching board and library for each device.

## Demonstrated behavior

The project demonstrated ESP32 temperature publication and robot-topic subscription using micro-ROS over Wi-Fi, with ROS graph, topic echo, and serial output used to inspect the data. The LoRa extension forwards packed robot telemetry to a separate receiver for serial inspection.

The transmitter limits its send schedule to one frame per 100 ms. This is a firmware setting, rather than a measured delivery-rate or range guarantee. See the [implementation notes](docs/implementation-notes.md) for sampling, freshness, and numeric-range details.

## Getting started

1. Install the appropriate ESP32/Heltec board support and the libraries listed in the [setup guide](docs/setup.md).
2. Create local configuration files for the two Wi-Fi sketches:

   ```bash
   cp firmware/temperature_node/secrets.example.h firmware/temperature_node/secrets.h
   cp firmware/sx1278_imu_transmitter/secrets.example.h firmware/sx1278_imu_transmitter/secrets.h
   ```

3. Set your Wi-Fi credentials and the micro-ROS agent's IP address in those local files.
4. Run the Humble micro-ROS agent on your ROS computer, matching the firmware's domain and UDP port:

   ```bash
   export ROS_DOMAIN_ID=10
   ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
   ```

5. Open each `.ino` in Arduino IDE, select its board, and upload it to the corresponding device. If both micro-ROS clients are used together, give each its own agent process and UDP port as described in the [setup guide](docs/setup.md#running-both-wi-fi-nodes).

Both micro-ROS sketches use domain **10**. Their local port configuration must match their respective agent process. The receiver needs no Wi-Fi credentials.

## Telemetry convention

The robot exposes IMU-related values through `Twist` fields:

| Field | Meaning in this project |
| --- | --- |
| `linear.x`, `linear.y`, `linear.z` | Unused by the reported robot publisher; still included in the packet |
| `angular.x` | Z-axis angular rate |
| `angular.y` | Estimated Z-axis bias |
| `angular.z` | Integrated yaw in degrees |

These are project-specific meanings, rather than the standard velocity interpretation of `Twist`. Scaling every field by 1000 before storing it in `int16_t` limits the representable range to **−32.768 through +32.767** in the field's original units. Yaw outside that range is clipped. The [packet specification](docs/packet-format.md) describes the exact encoding.

## Source and validation

The three implementations were recovered from the author's working project code. Publication preparation restored line wrapping from the source document and moved network configuration into ignored local headers. The firmware behavior and radio settings are retained.

Run the host packet checks with Python 3 and `g++`:

```bash
python3 tools/validate_packet.py
```

These checks exercise the packet structures, quantization, and CRC extracted from the actual sketches. They do not compile the complete Arduino firmware or replace device testing. Exact board-package and library versions from the original hardware setup were not recorded.

## Author and dependencies

Developed by **[Adhithya Palanisamy Chandrasekaran](https://github.com/ADHU-ADHU)**, with a focus on embedded systems, mechatronics, and robotics communication.

- [micro-ROS Arduino, Humble](https://github.com/micro-ROS/micro_ros_arduino/tree/humble)
- [Arduino-LoRa for SX127x radios](https://github.com/sandeepmistry/arduino-LoRa)
- [Heltec ESP32 board and library setup](https://docs.heltec.org/en/node/esp32/esp32_general_docs/quick_start.html)
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) and [OneWire](https://github.com/PaulStoffregen/OneWire)

Upstream libraries retain their respective licenses.
