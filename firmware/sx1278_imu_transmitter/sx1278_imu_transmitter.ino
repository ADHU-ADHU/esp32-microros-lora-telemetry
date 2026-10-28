#include <micro_ros_arduino.h>
#include "secrets.h"
#include <stdio.h>


#include <rcl/rcl.h>
#include <rcl/init_options.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>


#include <geometry_msgs/msg/twist.h>


#include <SPI.h>
#include <LoRa.h>


#define LORA_SCK    SCK
#define LORA_MISO   MISO
#define LORA_MOSI   MOSI
#define LORA_NSS    27
#define LORA_RST    33
#define LORA_DIO0   15


size_t domain_id = 10;


rcl_subscription_t imu_sub;
geometry_msgs__msg__Twist imu_msg;


rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rclc_executor_t executor;


#define RCCHECK(fn) { rcl_ret_t rc = fn; if(rc != RCL_RET_OK){ Serial.printf("RCL ERR %d at %s:%d\n", (int)rc, __FILE__, __LINE__); error_loop(); } }
#define RCSOFTCHECK(fn) { rcl_ret_t rc = fn; (void)rc; }


#if defined(LED_BUILTIN)
  #define LED_PIN LED_BUILTIN
#else
  #define LED_PIN 13
#endif


void error_loop() {
  while (1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(100);
  }
}


// ---- Packet ----
struct __attribute__((packed)) ImuPkt {
  uint8_t sync;
  uint8_t type;
  uint8_t seq;
  int16_t lin_x, lin_y, lin_z;
  int16_t ang_x, ang_y, ang_z;
  uint8_t crc;
};


static volatile bool imu_updated = false;
static geometry_msgs__msg__Twist imu_latest;
static uint8_t seq_counter = 0;


static inline int16_t f_to_i16(float v, float scale) {
  float s = v * scale;
  if (s > 32767.0f) s = 32767.0f;
  if (s < -32768.0f) s = -32768.0f;
  return (int16_t)(s);
}


uint8_t crc8_07(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
      else           crc <<= 1;
    }
  }
  return crc;
}


void imu_callback(const void * msgin) {
  const geometry_msgs__msg__Twist * msg = (const geometry_msgs__msg__Twist *)msgin;
  imu_latest = *msg;
  imu_updated = true;
}


bool send_imu_lora() {
  ImuPkt pkt;
  pkt.sync = 0xAA;
  pkt.type = 0x01;
  pkt.seq = seq_counter++;


 pkt.lin_x = f_to_i16((float)imu_latest.linear.x, 1000.0f);
 pkt.lin_y = f_to_i16((float)imu_latest.linear.y, 1000.0f);
 pkt.lin_z = f_to_i16((float)imu_latest.linear.z, 1000.0f);
    pkt.ang_x = f_to_i16((float)imu_latest.angular.x, 1000.0f);
    pkt.ang_y = f_to_i16((float)imu_latest.angular.y, 1000.0f);
    pkt.ang_z = f_to_i16((float)imu_latest.angular.z, 1000.0f);


    pkt.crc = crc8_07((uint8_t*)&pkt, sizeof(ImuPkt) - 1);


    Serial.printf("PACKED seq=%u lin(%.3f %.3f %.3f) ang(%.3f %.3f %.3f)\n",
                pkt.seq,
                pkt.lin_x/1000.0f, pkt.lin_y/1000.0f, pkt.lin_z/1000.0f,
                pkt.ang_x/1000.0f, pkt.ang_y/1000.0f, pkt.ang_z/1000.0f);


    LoRa.beginPacket();
    LoRa.write((uint8_t*)&pkt, sizeof(ImuPkt));
    int ok = LoRa.endPacket();
    return (ok == 1);
}


static uint32_t last_send_ms = 0;
static const uint32_t SEND_PERIOD_MS = 100;


void setup() {
  Serial.begin(115200);
  delay(1500);


    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);


    Serial.println("\n=== BOOT ===");


    // ---- LoRa init (with timeout) ---- just used ofr debugging
    Serial.println("[1] LoRa init...");
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);


    uint32_t t0 = millis();
    while (!LoRa.begin(433E6)) {
      if (millis() - t0 > 5000) {
        Serial.println("LoRa.begin FAILED (timeout). Check wiring/pins/frequency.");
        error_loop();
      }
      Serial.print(".");
      delay(200);
    }
    Serial.println("\nLoRa.begin OK");


    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.setPreambleLength(8);
    LoRa.setSyncWord(0x34);
    LoRa.enableCrc();


    Serial.println("[2] LoRa configured.");


    // ---- micro-ROS transport ----
    Serial.println("[3] Setting micro-ROS WiFi transport...");
    set_microros_wifi_transports(WIFI_SSID, WIFI_PASSWORD, MICRO_ROS_AGENT_IP, MICRO_ROS_AGENT_PORT);
  // Pinging agent to see if its reachable, .. just for debugging
  Serial.println("[4] Pinging micro-ROS agent...");
  bool agent_ok = false;
  for (int i = 0; i < 10; i++) {
    if (rmw_uros_ping_agent(1000, 1) == RMW_RET_OK) {
      agent_ok = true;
      break;
    }
    Serial.println(" ping failed, retry...");
  }
  if (!agent_ok) {
    Serial.println("Agent NOT reachable. Check agent IP/port, same WiFi, firewall, agent running udp4.");
    error_loop();
  }
    Serial.println("Agent reachable   ");


    allocator = rcl_get_default_allocator();


    Serial.println("[5] micro-ROS init (domain=10)...");
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
    RCCHECK(rcl_init_options_init(&init_options, allocator));
    RCCHECK(rcl_init_options_set_domain_id(&init_options, domain_id));


    RCCHECK(rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator));
    RCCHECK(rclc_node_init_default(&node, "sx1278_imu_lora_tx", "", &support));


    RCCHECK(rclc_subscription_init_default(
      &imu_sub,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
      "/robot/imu"
    ));


  RCCHECK(rclc_executor_init(&executor, &support.context, 1, &allocator));
  RCCHECK(rclc_executor_add_subscription(&executor, &imu_sub, &imu_msg, &imu_callback,
ON_NEW_DATA));


    Serial.println("[6] Ready: waiting for /robot/imu then sending LoRa.");
}


void loop() {
  RCSOFTCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));


    uint32_t now = millis();
    if (imu_updated && (now - last_send_ms >= SEND_PERIOD_MS)) {
      last_send_ms = now;
      bool ok = send_imu_lora();
      Serial.printf("LoRa TX seq=%u %s\n", (uint8_t)(seq_counter - 1), ok ? "OK" : "FAIL");
    }


    delay(5);
}
