# Implementation notes

The firmware retains the working project implementations supplied by the author. The public copy repairs document line wrapping and replaces embedded Wi-Fi/agent settings with local configuration headers. It does not redesign the original sampling or radio behavior.

## Temperature sampling

The DS18B20 node requests a conversion, checks a nominal 750 ms elapsed interval, and reads the result. It does not explicitly disable DallasTemperature's normal blocking conversion behavior. Temperature acquisition can therefore delay processing of incoming ROS messages. The two-second request interval is measured after the preceding read, so the sketch should be described as periodic rather than as a guaranteed 0.5 Hz publisher.

The IMU callback prints received fields. Its LED is set according to whether `linear.x` is nonzero; it does not toggle on every received message.

## IMU field semantics

The robot uses `geometry_msgs/msg/Twist` to carry angular rate, estimated bias, and integrated yaw. These meanings are specific to this project. A dedicated message type would make the interface clearer to other ROS systems.

## Latest-value transmission

The transmitter sets `imu_updated` after receiving its first ROS message and never clears it. From that point onward, it can resend the most recent sample every 100 ms even when no fresh ROS message arrives. The interval caps send attempts at 10 Hz; it does not establish 10 Hz fresh-sample delivery.

LoRa transmission uses the radio library's blocking send operation. The firmware has no agent reconnection state machine and its initialization errors enter an LED-blinking error loop. These behaviors are useful extension points for continuous unattended operation.

## Numeric limits

The fixed ×1000 scale limits every transmitted field to roughly ±32.768 original units. Full-range yaw in degrees can clip. A future protocol could use field-specific scales or a different yaw representation, while retaining a version/type byte for decoder compatibility.

The packet is a direct native-structure serialization. Both endpoint layout and byte order must match. See the [packet format](packet-format.md).

## Hardware and validation scope

The sketches retain the 433 MHz configuration used in the project. The exact Heltec product/radio variant, original Arduino core versions, and library versions are not fully recorded. Use a frequency-compatible radio variant and the appropriate board definitions.

The receiver prints validated data to serial; it is not a ROS 2 bridge back into the graph. Quantitative radio range, latency, packet-loss rate, and sustained throughput are not established by this repository. The host validation exercises the actual packet code but does not represent a full Arduino firmware build or a new hardware run.
