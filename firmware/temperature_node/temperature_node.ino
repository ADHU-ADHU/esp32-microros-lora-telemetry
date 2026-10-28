#include <micro_ros_arduino.h>
#include "secrets.h"
#include <stdio.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <geometry_msgs/msg/twist.h>
#include <std_msgs/msg/float32.h>
#include <OneWire.h>
#include <DallasTemperature.h>


//--------- HARDWARE --------------//
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


#if defined(LED_BUILTIN)
  #define LED_PIN LED_BUILTIN
#else
  #define LED_PIN 13
#endif

//        ROS 2 ENTITIES
// ============================
rcl_subscription_t imu_sub;
geometry_msgs__msg__Twist imu_msg;


rcl_publisher_t temp_pub;
std_msgs__msg__Float32 temp_msg;


rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rclc_executor_t executor;


size_t domain_id = 10;


#define RCCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){error_loop();} }
#define RCSOFTCHECK(fn) { rcl_ret_t temp_rc = fn; if((temp_rc != RCL_RET_OK)){} }


void error_loop() {
  while(1) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(100);
  }
}


//-----------   IMU SUBSCRIBER CALLBACK ----------//
void imu_callback(const void * msgin) {
  const geometry_msgs__msg__Twist * msg = (const geometry_msgs__msg__Twist *)msgin;


    Serial.println("---- IMU Data ----");
    Serial.print("Linear x: "); Serial.print(msg->linear.x);
    Serial.print(", y: "); Serial.print(msg->linear.y);
    Serial.print(", z: "); Serial.println(msg->linear.z);


    Serial.print("Angular x: "); Serial.print(msg->angular.x);
    Serial.print(", y: "); Serial.print(msg->angular.y);
    Serial.print(", z: "); Serial.println(msg->angular.z);


    // Toggle LED if linear.x != 0
    digitalWrite(LED_PIN, (msg->linear.x != 0) ? HIGH : LOW);
}


//            SETUP
unsigned long lastTempRequest = 0;
unsigned long tempRequestTime = 0;
bool tempRequested = false;


void setup() {
  Serial.begin(115200);
  delay(2000);


    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);


    sensors.begin(); // DS18B20 init


  // Connecting to Wi-Fi & Micro-ROS agent
  set_microros_wifi_transports(WIFI_SSID, WIFI_PASSWORD, MICRO_ROS_AGENT_IP, MICRO_ROS_AGENT_PORT); // IP of the computer running the ros2 agent


    allocator = rcl_get_default_allocator();


    // Configuring init options with domain
    rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
 RCCHECK(rcl_init_options_init(&init_options, allocator));
 RCCHECK(rcl_init_options_set_domain_id(&init_options, domain_id));


 // Init support + node
 RCCHECK(rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator));
 RCCHECK(rclc_node_init_default(&node, "arduino_imu_node", "", &support));


  // Creating a IMU subscriber of the topic /robot/imu which is published frm the robot
//running on pi4
  RCCHECK(rclc_subscription_init_default(
      &imu_sub,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
      "/robot/imu"));


 // Create temperature publisher
 RCCHECK(rclc_publisher_init_best_effort(
     &temp_pub,
     &node,
     ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
     "/esp32/temperature"));



  // Executor
  RCCHECK(rclc_executor_init(&executor, &support.context, 1, &allocator));
  RCCHECK(rclc_executor_add_subscription(&executor, &imu_sub, &imu_msg, &imu_callback,
ON_NEW_DATA));
}


//---------- Looping ------//
void loop() {
  unsigned long now = millis();


 // Spining executor to process IMU messages
 RCCHECK(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10)));


 // Requesting temperature every 2 seconds
 if (!tempRequested && now - lastTempRequest > 2000) {
   sensors.requestTemperatures(); // start DS18B20 conversion
   tempRequestTime = now;
   tempRequested = true;
 }




 // Reading temp after ~750 ms conversion
 if (tempRequested && now - tempRequestTime >= 750) {
   float t = sensors.getTempCByIndex(0);
   if (t > -100) {
     temp_msg.data = t;
     RCSOFTCHECK(rcl_publish(&temp_pub, &temp_msg, NULL));


     // Print temperature to Serial
     Serial.print("TEMP: ");
     Serial.print(t);
     Serial.println(" °C");
   }
   tempRequested = false;
        lastTempRequest = now;
    }


    delay(10);
}
