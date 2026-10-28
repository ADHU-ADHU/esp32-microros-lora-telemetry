# Hardware guide

The project combines two ESP32 micro-ROS clients with a separate LoRa receiver. This guide identifies the hardware families, connects them to the firmware, and records the product references supplied with the project.

## Controllers and host

### Adafruit HUZZAH32 ESP32 Feather

[![Adafruit HUZZAH32 ESP32 Feather reference](https://cdn-shop.adafruit.com/970x728/3405-08.jpg)](https://www.adafruit.com/product/3405)

The [Adafruit HUZZAH32 Feather, product 3405](https://www.adafruit.com/product/3405), is the ESP32 controller family identified in the project material. It provides 2.4 GHz Wi-Fi, USB-to-serial programming, and 3.3 V logic. Its Wi-Fi link connects the micro-ROS client to the ROS computer; it does not transmit LoRa without the external radio.

Two firmware roles use ESP32 boards:

- **Sensor node:** DS18B20 temperature publisher and `/robot/imu` subscriber.
- **Radio transmitter:** `/robot/imu` subscriber and external SX1278 packet transmitter.

The report identifies a Raspberry Pi 4 as the robot-side ROS publisher. A separate ROS 2 Humble computer runs the micro-ROS agent. Both firmware clients select ROS domain `10`. The agent's address and UDP port are local configuration values; the examples use port `8888`.

## Temperature probe

[![DS18B20 probe reference equivalent](https://cdn-shop.adafruit.com/970x728/381-01.jpg)](https://www.adafruit.com/product/381)

The project uses a **DS18B20** digital temperature sensor. The supplied hardware reference is the [Velleman/Whadda WPSE324 probe and adapter](https://www.velleman.eu/products/view/temperature-probe-ds18b20-arduino-compatible-adapter-wpse324/?id=459222). The photograph above is an [Adafruit DS18B20 probe reference equivalent](https://www.adafruit.com/product/381), not an assertion about the exact cable or adapter fitted to the project.

| Sensor property | Detail |
| --- | --- |
| Interface | 1-Wire digital bus |
| Sketch data pin | ESP32 GPIO4 |
| Programmable resolution | 9–12 bits |
| Temperature range | −55 to +125 °C |
| Specified accuracy | ±0.5 °C from −10 to +85 °C |
| 12-bit resolution step | 0.0625 °C |
| Maximum 12-bit conversion time | 750 ms |

The sensor specifications come from [Analog Devices' DS18B20 documentation](https://www.analog.com/en/products/DS18B20.html) and the [WPSE324 manual](https://media.distrelec.com/Web/Downloads/_m/an/WPSE324_eng_man.pdf). The firmware uses OneWire and DallasTemperature, then publishes degrees Celsius as `/esp32/temperature` (`std_msgs/msg/Float32`). Its sampling loop is periodic, not a benchmarked fixed-rate publisher.

Use a suitable pull-up for the selected probe/adapter and verify its wiring. The WPSE324 manual identifies red as supply, black as ground, and yellow as data; confirm the actual supplied cable and board rather than relying on colors alone.

## SX1278 transmitter

![SX1278 433 MHz V4.0 module shown in the project material](assets/sx1278-module.png)

This module image is directly extracted from the project material. The pictured board is marked **SX1278 LoRa 433 MHz V4.0** and includes a spring antenna. The report's [original supplier link](https://share.temu.com/HkFaDMhggxB) identifies the listing; the breakout manufacturer and a formal module part number are not recorded.

The [SX1278 radio family](https://www.semtech.com/products/wireless-rf/lora-connect) supplies the LoRa physical layer. In the project, an external ESP32 drives it through SPI using Sandeep Mistry's Arduino-LoRa library.

| Signal | Project firmware setting |
| --- | --- |
| CS / NSS | GPIO27 |
| RESET | GPIO33 |
| DIO0 | GPIO15 |
| SCK / MISO / MOSI | Selected board's Arduino pin constants |
| Radio frequency | 433 MHz |

The sketch explicitly configures SF7, 125 kHz bandwidth, coding rate 4/5, an eight-symbol preamble, sync word `0x34`, and radio CRC. It does not set an explicit transmit power, so no dBm value is claimed here. See [the transmitter source](../firmware/sx1278_imu_transmitter/sx1278_imu_transmitter.ino).

## Heltec receiver

[![Heltec Wireless Stick Lite V3 product-family reference](https://heltec.org/wp-content/uploads/2023/09/wireless-stickLite-front.png)](https://heltec.org/project/wireless-stick-lite-v2/)

The report names a Wireless Stick V3 and links to [Heltec's Wireless Stick Lite product page](https://heltec.org/project/wireless-stick-lite-v2/), which currently identifies the **Wireless Stick Lite V3** family. That family combines an **ESP32-S3FN8** with an **SX1262** radio. The product photograph is a manufacturer reference, not a photograph proving the exact receiver variant used on the bench.

The receiver uses the Heltec library's `LoRaWan_APP.h` radio API. It initializes the board through `HELTEC_BOARD`, sets continuous reception, validates the binary packet, and writes decoded values plus RSSI/SNR diagnostics to serial. It does not use Wi-Fi or return the data to ROS 2.

### Receiver variant and radio band

The project firmware sets **433 MHz**, while the receiver product image embedded in the original material is visibly marked **EU868**. This mismatch does not establish the fitted RF front end or antenna of the actual tested receiver. The precise board variant remains unconfirmed.

An SX1262 chip's tuning range is not sufficient to establish a complete board's supported band: RF matching, filters, and antenna also matter. Match the actual transmitter, receiver variant, and antennas to the configured carrier. Use the [Heltec product documentation](https://heltec.org/project/wireless-stick-lite-v2/) and the markings/datasheet for the exact board in hand.

The sketches use a **direct LoRa link**. The Heltec header's name does not imply a LoRaWAN gateway or network-server deployment.

## Image credits and source notes

| Picture | Credit and provenance |
| --- | --- |
| HUZZAH32 Feather | Adafruit product 3405 manufacturer reference image |
| DS18B20 probe | Adafruit product 381 reference-equivalent image; project material references WPSE324 |
| SX1278 module | Standalone product photo supplied in the author's source material, Appendix B; original supplier link above |
| Heltec board | Heltec Wireless Stick Lite product-family reference image |

The source PDFs are not included in this repository. Only the standalone radio product picture was extracted; screenshots containing code, network settings, or credentials are excluded.
