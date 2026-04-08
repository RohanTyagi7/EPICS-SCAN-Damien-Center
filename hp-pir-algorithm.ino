#include <stdexcept>
#include <queue>
#include <esp_sleep.h>
#include "MyLD2410.h" 

// define RX and TX pins by microcontroller model
#if defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_LEONARDO) 
  // ARDUINO_SAMD_NANO_33_IOT RX_PIN is D1, TX_PIN is D0 
  // ARDUINO_AVR_LEONARDO RX_PIN(RXI) is D0, TX_PIN(TXO) is D1 
  #define sensorSerial Serial1 
#elif defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6) 
  // RX_PIN is D7, TX_PIN is D6 
  #define sensorSerial Serial0 
#elif defined(ESP32) 
  // Other ESP32 device - choose available GPIO pins 
  #define sensorSerial Serial1 
  #if defined(ARDUINO_ESP32S3_DEV) 
    #define RX_PIN 18 
    #define TX_PIN 17 
  #else 
    #define RX_PIN 16 
    #define TX_PIN 17 
  #endif 
#else 
  #error "This sketch only works on ESP32, Arduino Nano 33IoT, and Arduino Leonardo (Pro-Micro)" 
#endif

// #define SENSOR_DEBUG 
#define ENHANCED_MODE 
#define SERIAL_BAUD_RATE 115200

#ifdef SENSOR_DEBUG
  MyLD2410 sensor(sensorSerial, true); 
#else 
  MyLD2410 sensor(sensorSerial); 
#endif

#define PIR_PIN GPIO_NUM_14
#define LED_PIN GPIO_NUM_13
#define MOSFET_PIN GPIO_NUM_27
#define BUTTON_PIN GPIO_NUM_18

#define WAITING 1
#define WATCHING 2

#define DISTANCE_SAMPLE_DELAY 1000  // time between distance samples (ms)

#define MOTION_TIMEOUT 90000  // motion timeout threshold (ms)

#define ROOM_EMPTY_TIMEOUT 5000  // room empty timeout (ms)

#define MOTION_VAL_THRESHOLD 100  // threshold for average motion value over sample interval

#define MAX_DISTANCE_DATA_POINTS 10  // amount of values stored in the distance data queue

#define LOOP_DEBUG

unsigned long power_on_time;  // time of power-on
unsigned long loop_start_time;  // time when loop started
unsigned long loop_end_time;  // time when loop ended
unsigned long dt;  // time that loop took to execute

unsigned long distance_measurement_timer = 0;  // timer to control distance measurements

unsigned long room_empty_time;  // amount of time room has been empty 
unsigned long motion_time;  // amount of time since motion occured
unsigned short system_state = WAITING;

bool is_emergency_occurring = false;

std::queue<unsigned long> distance_data;

void init_presence_sensor() {
  digitalWrite(MOSFET_PIN, HIGH);

  #if defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_LEONARDO) 
    sensorSerial.begin(LD2410_BAUD_RATE); 
  #else 
    sensorSerial.begin(LD2410_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN); 
  #endif

  delay(300);  // give sensor time to wake up

  while (!sensor.begin()) { 
    Serial.println("Failed to communicate with the sensor."); 
    delay(1200);
  }

  #ifdef ENHANCED_MODE 
    sensor.enhancedMode(); 
  #else 
    sensor.enhancedMode(false); 
  #endif
}

void setup() {
  power_on_time = millis();

  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(PIR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);

  // for testing purposes to power button
  pinMode(GPIO_NUM_21, OUTPUT);
  digitalWrite(GPIO_NUM_21, HIGH);

  digitalWrite(LED_PIN, LOW);

  Serial.println(__FILE__); 

  init_presence_sensor();

  distance_data.push(0);  // initialize distance array with a zero value

  // TESTING PURPOSES
  Serial.println("Collecting data in 5...");
  delay(1000);
  Serial.println("4");
  delay(1000);
  Serial.println("3");
  delay(1000);
  Serial.println("2");
  delay(1000);
  Serial.println("1");
  delay(1000);

  loop_start_time = millis();
}

void loop() {
  if (digitalRead(BUTTON_PIN) == HIGH) {
    is_emergency_occurring = true;
  }
  
  if (is_emergency_occurring) {
    digitalWrite(LED_PIN, HIGH);

    Serial.print("Emergency triggered at ");
    Serial.print(millis() - power_on_time);
    Serial.println(" ms.");

    // cancel emergency
    while (digitalRead(BUTTON_PIN) == HIGH) {
      // if button already pressed, let it go low
    }

    while (digitalRead(BUTTON_PIN) == LOW) {
      // wait for button to go high (manual emergency cancellation)
    }

    delay(50); // debounce, give time 

    // cancel emergency
    while (digitalRead(BUTTON_PIN) == HIGH) {
      // wait until button is let go
    }

    digitalWrite(LED_PIN, LOW);

    is_emergency_occurring = false;
    room_empty_time = 0;
    motion_time = 0;
  }

  if (system_state == WAITING) {
    if (digitalRead(PIR_PIN) == 1) {
      system_state = WATCHING;
      room_empty_time = 0;
      motion_time = 0;

      power_on_hp();

      loop_start_time = millis();
      dt = 0;
    } else {
      delay(100);
    }
  }
  
  if (system_state == WATCHING && room_empty_time > ROOM_EMPTY_TIMEOUT) {
    system_state = WAITING;
    power_off_hp();
    enter_deep_sleep();
  } else if (system_state == WATCHING) {
    update_values(dt);

    if (motion_time > MOTION_TIMEOUT) {
      is_emergency_occurring = true;
    }
  }

  // HANDLE MANUAL ALERT
  // NOT YET IMPLEMENTED

  // HANDLE LOOP DEBUG PRINT STATEMENTS
  #ifdef LOOP_DEBUG
    Serial.print(millis() - power_on_time);  // time since power-on (ms)
    Serial.print(" | ");

    switch (system_state) {
      case WAITING:
        Serial.print("WAITING");
        break;
      case WATCHING:
        Serial.print("WATCHING");
        break;
    }

    Serial.print(" | ");
    Serial.print(motion_time);
    Serial.print(" | ");
    Serial.print(room_empty_time);
    Serial.print(" | ");

    if (sensor.presenceDetected()) {
      Serial.print(sensor.detectedDistance());
    } else {
      Serial.print("null");
    }
    Serial.print(" | ");

    Serial.print(calc_estimated_distance());
    Serial.print(" | ");
    
    // print instantaneous motion value
    sensor.presenceDetected() && sensor.movingTargetDetected() 
      ? Serial.print(sensor.movingTargetSignal())
      : Serial.print("0");

    Serial.print(" | ");

    // print scaled motion value
    sensor.presenceDetected() && sensor.movingTargetDetected() 
      ? Serial.println(scale_motion(sensor.movingTargetSignal(), calc_estimated_distance()))
      : Serial.println("0");
  #endif

  loop_end_time = millis();
  dt = loop_end_time - loop_start_time;
  loop_start_time = loop_end_time;

  distance_measurement_timer += dt;
}

void power_off_hp() {
  // power off human presence sensor
  // NOT YET IMPLEMENTED
  
  digitalWrite(MOSFET_PIN, LOW);  // Turn off applied voltage

  Serial.print("Human presence 'power off' at ");
  Serial.print(millis() - power_on_time);
  Serial.println(" ms.");
}

void power_on_hp() {
  // power on human presence sensor
  digitalWrite(MOSFET_PIN, HIGH); // Apply voltage to the mosfet gate

  delay(100);  // allow LD2410 to wake-up

  init_presence_sensor();

  Serial.print("Human presence 'power on' at ");
  Serial.print(millis() - power_on_time);
  Serial.println(" ms.");
}

void update_values(unsigned long dt) {
  unsigned char motion_val = 0;

  while (sensor.check() != MyLD2410::Response::DATA) {
    // wait for new values to update

    // keep checking for emergency button even while waiting
    if (digitalRead(BUTTON_PIN) == HIGH) {
      is_emergency_occurring = true;
    }
  }

  if (!sensor.presenceDetected()) {
    room_empty_time += dt;
  } else if (sensor.presenceDetected() && sensor.movingTargetDetected()) {
    motion_val = sensor.movingTargetSignal();
    motion_val = scale_motion(motion_val, calc_estimated_distance());
  } else {
    motion_val = 0;
    room_empty_time = 0;
  }

  if (sensor.presenceDetected()) {
    room_empty_time = 0;

    if (distance_measurement_timer > DISTANCE_SAMPLE_DELAY) {
      distance_measurement_timer = 0;
      // add new value to distance data
      if (distance_data.size() >= MAX_DISTANCE_DATA_POINTS) {
        distance_data.pop();
      }
      distance_data.push(sensor.detectedDistance());
    }
  }

  if (motion_val >= MOTION_VAL_THRESHOLD) {
    motion_time = 0;
  } else {
    motion_time += dt;
  }
}

unsigned long calc_estimated_distance() {
  unsigned long min_distance_value = -1;

  std::queue<unsigned long> data_copy = distance_data;
  while (!data_copy.empty()) {
    if (data_copy.front() < min_distance_value || min_distance_value == -1) {
      min_distance_value = data_copy.front();
    }
    data_copy.pop();
  }

  return(min_distance_value);
}

void enter_deep_sleep() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause(); 
  Serial.println("Previous wakeup reason: " + String(wakeup_reason)); 
  esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH); 
  Serial.println("GPIO wakeup enabled"); 
  delay(100); 
  Serial.println("Entering deep sleep... Move in front of PIR to wake up."); 
  esp_deep_sleep_start();
}

unsigned char scale_motion(unsigned int motion_val, unsigned long estimated_distance) {
  /* estimated_distance: distance from person to sensor in centimeters */
  estimated_distance *= 0.0328;  // convert from cm to feet
  float scale;
  if (estimated_distance < 4) {
    scale = 1.0;
  } else if (4 <= estimated_distance && estimated_distance < 7) {
    scale = 1.0/3 * (estimated_distance - 4) + 1;
  } else if (7 <= estimated_distance && estimated_distance < 9) {
    scale = 1.0/4 * (estimated_distance - 7) + 2;
  } else {
    scale = 2.5;
  }

  motion_val *= scale;
  if (motion_val > 100) {
    motion_val = 100;
  }

  return((unsigned char)motion_val);
}
