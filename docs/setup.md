# Setup and wiring

## Hardware roles

| Device | Components | Connections |
| --- | --- | --- |
| Temperature node | ESP32, DS18B20, suitable sensor pull-up | DS18B20 data on GPIO4; supply and ground wired for the selected sensor/board |
| LoRa transmitter | ESP32, SX1278 radio, matching antenna | SPI plus NSS/CS GPIO27, RESET GPIO33, DIO0 GPIO15 |
| LoRa receiver | Compatible Heltec ESP32-S3/SX1262 board and antenna | Radio wiring supplied by the selected Heltec board definition |

The transmitter uses the selected Arduino board's `SCK`, `MISO`, and `MOSI` constants. Their numeric pins depend on the board definition. Do not substitute arbitrary GPIO numbers without checking your board's pinout.

Both radio sketches retain the project's **433 MHz** configuration. Use a board/radio variant and antenna that support that frequency. A generic Heltec product name does not establish the fitted radio variant; verify the exact hardware before reproducing the link.

## Libraries

| Sketch | Dependencies |
| --- | --- |
| Temperature node | ESP32 Arduino core, `micro_ros_arduino` for Humble, OneWire, DallasTemperature |
| SX1278 transmitter | ESP32 Arduino core, `micro_ros_arduino` for Humble, SPI, Sandeep Mistry's Arduino-LoRa |
| Heltec receiver | Compatible Heltec board support and library providing `LoRaWan_APP.h` and `Arduino.h` |

Follow the [micro-ROS Humble Arduino documentation](https://github.com/micro-ROS/micro_ros_arduino/tree/humble) for its supported boards, precompiled library, and Arduino-core compatibility. The project's original version combination is not pinned here.

The [Arduino-LoRa library](https://github.com/sandeepmistry/arduino-LoRa) targets SX127x radios. It is not the receiver's SX1262 driver. Follow [Heltec's setup documentation](https://docs.heltec.org/en/node/esp32/esp32_general_docs/quick_start.html) for the board/library combination that provides the receiver's API; that page also links to its newer documentation site.

## Local network configuration

Copy `secrets.example.h` to `secrets.h` in each Wi-Fi sketch's own folder. Edit the copies to configure:

| Setting | Purpose |
| --- | --- |
| `WIFI_SSID` | Your Wi-Fi network |
| `WIFI_PASSWORD` | Your Wi-Fi password |
| `MICRO_ROS_AGENT_IP` | Reachable IP address of the computer running that sketch's agent |
| `MICRO_ROS_AGENT_PORT` | Agent UDP port; default 8888 |

The example IP address is documentation-only and must be replaced. Local `secrets.h` files are ignored by Git. The Heltec receiver has no Wi-Fi configuration.

## Start the ROS side

Use ROS 2 Humble with a compatible `micro_ros_agent` installation. In each ROS terminal:

```bash
source /opt/ros/humble/setup.bash
export ROS_DOMAIN_ID=10
```

Start an agent for the Wi-Fi sketch being tested:

```bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

Upload the chosen sketch in Arduino IDE, selecting the correct board and serial port. The sketches print at **115200 baud**.

The firmware expects a robot publisher on `/robot/imu` using `geometry_msgs/msg/Twist`. If that publisher is unavailable, this synthetic message can check topic reception while the selected ESP32 sketch is running:

```bash
ros2 topic pub --rate 2 /robot/imu geometry_msgs/msg/Twist \
  '{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.1, y: 0.0, z: 12.0}}'
```

This publishes test telemetry; it is not a measurement from a robot. For the temperature node, inspect its topic with compatible best-effort QoS:

```bash
ros2 topic echo /esp32/temperature --qos-reliability best_effort
```

The temperature sketch logs incoming robot values and its outgoing temperature values. The transmitter logs initialization and packet sends; the receiver logs payload length, RSSI/SNR, packet checks, and decoded values.

## Running both Wi-Fi nodes

The temperature node and SX1278 transmitter can be brought up separately while checking their firmware. For simultaneous use, run separate agent processes on distinct UDP ports and point each sketch at the corresponding port in its `secrets.h`:

```bash
# Agent for the temperature node
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

```bash
# In another domain-10 terminal: agent for the SX1278 transmitter
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8889
```

Set the transmitter's `MICRO_ROS_AGENT_PORT` to 8889 for this configuration. This separates the clients' transport endpoints while keeping them in the same ROS domain. It is an example reproduction arrangement, not a claim about the exact agent-process layout used in the original demonstration.

## Radio settings

| Setting | Transmitter / receiver |
| --- | --- |
| Frequency | 433 MHz |
| Spreading factor | 7 |
| Bandwidth | 125 kHz |
| Coding rate | 4/5 |
| Preamble | 8 symbols |
| Header | Variable length |
| PHY CRC | Enabled |
| Network sync | Public network; transmitter sync word `0x34` |
| Application payload | 16 bytes, with a separate CRC-8 |

The settings must match on both ends. `LoRaWan_APP.h` is the Heltec library header used to access its radio API here; this is a direct LoRa link, not a LoRaWAN network-server integration.
