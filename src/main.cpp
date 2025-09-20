#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>  // เพิ่มไลบรารีสำหรับ PZEM004T

// กำหนดขา MAX485 (ใช้เลขขาจริงแทนตัวแปร A4, A5)
#define MAX485_DE      2 // ใช้ขา Digital 2 แทน A4
#define MAX485_RE      3 // ใช้ขา Digital 3 แทน A5

// กำหนดขาวัดระดับน้ำ
#define WATER_LEVEL_PIN 54 // ขา A0 มีค่าเท่ากับ Digital 54 บน Arduino Mega

// กำหนดขาที่เชื่อมต่อกับเซนเซอร์วัดอัตราการไหลของน้ำ
#define FLOW_SENSOR_1 22  // Digital pin 22
#define FLOW_SENSOR_2 23  // Digital pin 23
#define FLOW_SENSOR_3 24  // Digital pin 24

// === Relay Control System ===
// กำหนดขาที่จะควบคุม relay (8 ขา) - ตามลำดับ K1-K8
// K1=26, K2=28, K3=30, K4=27, K5=33, K6=31, K7=29, K8=32
int relayPins[] = {26, 28, 30, 27, 33, 31, 29, 32};  

// กำหนดประเภทของแต่ละ relay (true = Active High, false = Active Low)
bool relayActiveHigh[] = {true, false, false, false, false, false, false, false}; // K1=Active High, K2-K8=Active Low

int relayPinCount = sizeof(relayPins) / sizeof(relayPins[0]);
String lastRelayCommand = "00000000"; // เก็บคำสั่งล่าสุด
bool relayStates[8] = {false}; // เก็บสถานะปัจจุบันของแต่ละ relay

// สร้าง PZEM004Tv30 object สำหรับวัดไฟฟ้า (Serial3: ขา 14 = TX3, 15 = RX3 บน Arduino Mega)
PZEM004Tv30 pzem(Serial3);

// สร้าง ModbusMaster objects สำหรับแต่ละเซ็นเซอร์
ModbusMaster co2Sensor;    // ID 1: CO2, Temp, Humidity
ModbusMaster lightSensor;  // ID 2: Light Intensity
ModbusMaster ecSensor;     // ID 3: EC
ModbusMaster phSensor;     // ID 4: PH & Temp

// ตัวแปรเก็บค่าจากเซ็นเซอร์
// CO2 Sensor (ID 1)
float airTemp = 0.0;
float airHumidity = 0.0;
int co2Ppm = 0;

// Light Sensor (ID 2)
uint32_t luxValue = 0;

// EC Sensor (ID 3)
float ecValue = 0.0;
float ecCalibration = 0.0;
bool isEcSensorRange4400 = true; // true = 0~4400 uS/cm, false = 0~44000 uS/cm

// PH Sensor (ID 4)
float phValue = 0.0;
float waterTemp = 0.0;

// Water Level Sensor (Digital, connected to A0)
bool waterDetected = false;

// AC Power Sensor (PZEM-004T v3.0) ตัวแปรสำหรับเก็บค่าพลังงานไฟฟ้า
float acVoltage = 0.0;
float acCurrent = 0.0;
float acPower = 0.0;
float acEnergy = 0.0;
float acFrequency = 0.0;
float acPowerFactor = 0.0;
bool acSensorConnected = false;

// ตัวแปรสำหรับเวลา
unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;
unsigned long lastAcReadTime = 0;  // เพิ่มตัวแปรสำหรับอ่านค่า AC
const unsigned long READ_INTERVAL = 1000;  // อ่านค่าทุก 1 วินาที
const unsigned long SEND_INTERVAL = 2000; // ส่งข้อมูลไปยัง ESP32 ทุก 2 วินาที (เร็วขึ้น)
const unsigned long AC_READ_INTERVAL = 1000;  // อ่านค่า AC ทุก 1 วินาที

// ตัวแปรสำหรับการทดสอบและตรวจสอบการสื่อสาร
unsigned long lastCommunicationTest = 0;
const unsigned long COMM_TEST_INTERVAL = 30000; // ทดสอบการสื่อสารทุก 30 วินาที (ลดลง)
bool communicationOK = false;
int sendAttempts = 0;
const int MAX_SEND_ATTEMPTS = 3;

// ตัวแปรสำหรับเก็บสถานะปัจจุบันและสถานะก่อนหน้า
int currentState1 = 0, lastState1 = 0;
int currentState2 = 0, lastState2 = 0;
int currentState3 = 0, lastState3 = 0;

// ตัวแปรสำหรับเก็บจำนวนพัลส์
int pulseCount1 = 0;
int pulseCount2 = 0;
int pulseCount3 = 0;

// ตัวแปรสำหรับคำนวณอัตราการไหล
float flowRate1 = 0.0;
float flowRate2 = 0.0;
float flowRate3 = 0.0;

// ตัวแปรเก็บปริมาณน้ำสะสม (มิลลิลิตร)
float totalMilliLitres1 = 0;
float totalMilliLitres2 = 0;
float totalMilliLitres3 = 0;

// ตัวแปรเก็บเวลา
unsigned long oldTime = 0;

// ค่าคงที่สำหรับแปลงพัลส์เป็นอัตราการไหล
const float calibrationFactor = 7.5; // พัลส์ต่อวินาทีต่อลิตรต่อนาที

// === EC CALIBRATION EQUATION: y = 15.968x - 53.913, R² = 0.9973 ===
// สมการหลักสำหรับการคำนวณค่า EC ที่แม่นยำ
const float EC_SLOPE = 15.968;      // ความชัน (m) 
const float EC_INTERCEPT = -53.913; // จุดตัดแกน y (b)
// x = ค่า raw จากเซ็นเซอร์, y = ค่า EC ที่แคลิเบรตแล้ว (uS/cm)

// === ULTRA-PRECISE TIMING VARIABLES ===
// ตัวแปรสำหรับปั๊ม EC
unsigned long ecPumpStartTime = 0;
unsigned long ecPumpDuration = 0;
bool ecPumpRunning = false;

// ตัวแปรสำหรับปั๊ม PH
unsigned long phPumpStartTime = 0;
unsigned long phPumpDuration = 0;
bool phPumpRunning = false;

// ตัวแปรสำหรับ Internal Fan cycle
unsigned long fanCycleStartTime = 0;
bool fanCycleState = false; // false = OFF period, true = ON period
unsigned long fanDelayOn = 0;
unsigned long fanDelayOff = 0;
int fanRelayIndex = 0;
bool fanCycleActive = false;

// ฟังก์ชันควบคุมการส่ง/รับข้อมูลผ่าน MAX485
void preTransmission() {
  digitalWrite(MAX485_RE, HIGH);
  digitalWrite(MAX485_DE, HIGH);
}

void postTransmission() {
  digitalWrite(MAX485_RE, LOW);
  digitalWrite(MAX485_DE, LOW);
}

void readCO2Sensor();
void readLightSensor();
void readECSensor();
void readPHSensor();
void readWaterLevel();
void printAllValues();
void checkFlowSensors();
void readACPowerSensor();
void sendDataToESP32();
void receiveCommandFromESP32();
void testESP32Communication();
void testACPowerSensor();
void checkPumpTiming();

// === Calibration Functions ===
float calibrateEC(float rawValue);

// === Relay Control Functions ===
void initRelays();
void applyRelayCommand(String command);
void printRelayStatus();

void setup() {
  // เริ่มต้น Serial Monitor
  Serial.begin(115200);
  Serial.println("เริ่มต้นการทำงานเซ็นเซอร์...");
  
  // เริ่มต้น Serial2 สำหรับสื่อสารกับ ESP32
  Serial2.begin(115200);
  
  // เริ่มต้น Serial1 สำหรับ Modbus RTU (ขา 18=TX1, 19=RX1 บน Arduino Mega)
  Serial1.begin(9600, SERIAL_8N1);
  
  // เริ่มต้น Serial3 สำหรับ PZEM-004T (ขา 14=TX3, 15=RX3 บน Arduino Mega)
  Serial3.begin(9600, SERIAL_8N1);
  
  // ตั้งค่าขา MAX485
  pinMode(MAX485_DE, OUTPUT);
  pinMode(MAX485_RE, OUTPUT);
  digitalWrite(MAX485_RE, LOW);
  digitalWrite(MAX485_DE, LOW);
  
  // ตั้งค่าขาวัดระดับน้ำ
  pinMode(WATER_LEVEL_PIN, INPUT);
  
  // ตั้งค่า CO2 Sensor (ID 1)
  co2Sensor.begin(1, Serial1);
  co2Sensor.preTransmission(preTransmission);
  co2Sensor.postTransmission(postTransmission);
  
  // ตั้งค่า Light Sensor (ID 2)
  lightSensor.begin(2, Serial1);
  lightSensor.preTransmission(preTransmission);
  lightSensor.postTransmission(postTransmission);
  
  // ตั้งค่า EC Sensor (ID 3)
  ecSensor.begin(3, Serial1);
  ecSensor.preTransmission(preTransmission);
  ecSensor.postTransmission(postTransmission);
  
  // ตั้งค่า PH Sensor (ID 4)
  phSensor.begin(4, Serial1);
  phSensor.preTransmission(preTransmission);
  phSensor.postTransmission(postTransmission);
  
  // ตั้งค่าสำหรับเซนเซอร์วัดอัตราการไหล
  pinMode(FLOW_SENSOR_1, INPUT);
  pinMode(FLOW_SENSOR_2, INPUT);
  pinMode(FLOW_SENSOR_3, INPUT);
  
  // เปิดใช้งาน Pull-Up Resistor ภายใน
  digitalWrite(FLOW_SENSOR_1, HIGH);
  digitalWrite(FLOW_SENSOR_2, HIGH);
  digitalWrite(FLOW_SENSOR_3, HIGH);
  
  // อ่านสถานะเริ่มต้น
  lastState1 = digitalRead(FLOW_SENSOR_1);
  lastState2 = digitalRead(FLOW_SENSOR_2);
  lastState3 = digitalRead(FLOW_SENSOR_3);
  
  oldTime = millis();
  
  Serial.println("Modbus Ready");
  Serial.println("CO2:ID1 Light:ID2 EC:ID3 PH:ID4");
  Serial.println("EC Calib: y=15.968x-53.913");
  Serial.println("Water:A0 AC:Serial3 Flow:D22-24");
  
  // เริ่มต้นระบบ Relay Control
  initRelays();
  Serial.println("Relay System: Ready (K1-K8 on pins 26,28,30,27,33,31,29,32)");
  
  // ทดสอบการสื่อสารกับ ESP32
  testESP32Communication(); // เปิดการทดสอบ ESP32
  
  // ทดสอบการเชื่อมต่อกับ PZEM-004T
  testACPowerSensor();
  
  // รอให้ระบบเริ่มต้นทำงาน
  delay(2000);
}

void loop() {
  // ตรวจสอบการสื่อสารกับ ESP32 เป็นระยะ
  if (millis() - lastCommunicationTest >= COMM_TEST_INTERVAL) {
    lastCommunicationTest = millis();
    testESP32Communication();
  }

  // อ่านค่าเซ็นเซอร์ทั่วไปทุกๆ READ_INTERVAL ms
  if (millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();
    
    readCO2Sensor();
    readLightSensor();
    readECSensor();
    readPHSensor();
    readWaterLevel();
    
    // แสดงค่าทั้งหมดบน Serial Monitor
    printAllValues();
  }
  
  // ตรวจสอบและคำนวณอัตราการไหลของน้ำ
  checkFlowSensors();
  
  // อ่านค่าเซ็นเซอร์ AC ทุกๆ AC_READ_INTERVAL ms
  if (millis() - lastAcReadTime >= AC_READ_INTERVAL) {
    lastAcReadTime = millis();
    readACPowerSensor();
  }
  
  // ส่งข้อมูลไปยัง ESP32 ทุกๆ SEND_INTERVAL ms (ไม่ต้องรอการตอบกลับ)
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();
    sendDataToESP32();
  }
  
  // รับคำสั่งจาก ESP32 (ถ้ามี)
  receiveCommandFromESP32();
  
  // === ULTRA-PRECISE TIMING SYSTEM ===
  // ตรวจสอบการจับเวลาปั๊ม EC และ PH แบบแม่นยำสูงสุด
  checkPumpTiming();
}

// === ULTRA-PRECISE TIMING FUNCTION ===
void checkPumpTiming() {
  unsigned long currentTime = millis();
  
  // ตรวจสอบ Internal Fan cycle (ใช้ global variables)
  
  if (fanCycleActive) {
    unsigned long elapsedTime;
    
    // ป้องกัน overflow
    if (currentTime >= fanCycleStartTime) {
      elapsedTime = currentTime - fanCycleStartTime;
    } else {
      elapsedTime = (0xFFFFFFFF - fanCycleStartTime) + currentTime + 1;
    }
    
    unsigned long cycleInterval = fanCycleState ? fanDelayOn : fanDelayOff;
    
    if (elapsedTime >= cycleInterval) {
      fanCycleState = !fanCycleState; // สลับสถานะ
      fanCycleStartTime = currentTime;
      
      // ควบคุม relay
      relayStates[fanRelayIndex] = fanCycleState;
      if (relayActiveHigh[fanRelayIndex]) {
        digitalWrite(relayPins[fanRelayIndex], fanCycleState ? HIGH : LOW);
      } else {
        digitalWrite(relayPins[fanRelayIndex], fanCycleState ? LOW : HIGH);
      }
      
      // คำนวณความแม่นยำ
      float accuracy = 100.0 - (abs((long)(elapsedTime - cycleInterval)) * 100.0 / cycleInterval);
      String stateStr = fanCycleState ? "ON" : "OFF";
      String periodStr = fanCycleState ? "OFF" : "ON";
      
      Serial.println("🌀 MEGA Internal Fan: " + stateStr + " period started after " + String(elapsedTime) + " ms");
      Serial.println("   Previous " + periodStr + " period: " + String(cycleInterval/1000) + "s (Target: " + String(cycleInterval/1000) + "s)");
      Serial.println("⚡ Timing Accuracy: " + String(accuracy, 2) + "% (Error: ±" + String(abs((long)(elapsedTime - cycleInterval))) + "ms)");
      
      // ส่งสถานะกลับไป ESP32
      Serial2.println("FAN_CYCLE_STATE:" + String(fanCycleState ? "ON" : "OFF") + "," + String(elapsedTime) + "," + String(accuracy, 2));
    }
  }
  
  // 🔥 ULTRA-PRECISE EC PUMP TIMING - ตรวจสอบทุก loop
  if (ecPumpRunning && ecPumpDuration > 0 && ecPumpStartTime > 0) {
    unsigned long elapsedTime;
    
    // ป้องกัน overflow
    if (currentTime >= ecPumpStartTime) {
      elapsedTime = currentTime - ecPumpStartTime;
    } else {
      elapsedTime = (0xFFFFFFFF - ecPumpStartTime) + currentTime + 1;
    }
    
    // DEBUG: แสดงสถานะปัจจุบันทุก 2 วินาที
    static unsigned long lastDebugTime = 0;
    if (currentTime - lastDebugTime >= 2000) { // ทุก 2 วินาที
      lastDebugTime = currentTime;
      Serial.println("🔍 EC Pump ULTRA-PRECISE DEBUG:");
      Serial.println("   - Running: " + String(ecPumpRunning));
      Serial.println("   - Duration: " + String(ecPumpDuration) + " ms");
      Serial.println("   - Start Time: " + String(ecPumpStartTime) + " ms");
      Serial.println("   - Current Time: " + String(currentTime) + " ms");
      Serial.println("   - Elapsed: " + String(elapsedTime) + " ms");
      Serial.println("   - Remaining: " + String(ecPumpDuration - elapsedTime) + " ms");
      Serial.println("   - Progress: " + String((elapsedTime * 100.0 / ecPumpDuration), 1) + "%");
    }
    
    // 🔥 ULTRA-PRECISE: ตรวจสอบเวลาทุก loop (ไม่ใช่ทุก 1 วินาที)
    if (elapsedTime >= ecPumpDuration) {
      // ปิดปั๊ม EC ทันที (K7 = position 6)
      relayStates[6] = false; // K7 OFF
      if (relayActiveHigh[6]) {
        digitalWrite(relayPins[6], LOW);
      } else {
        digitalWrite(relayPins[6], HIGH);
      }
      
      ecPumpRunning = false;
      
      // คำนวณความแม่นยำแบบ Ultra-Precise
      float accuracy = 100.0 - (abs((long)(elapsedTime - ecPumpDuration)) * 100.0 / ecPumpDuration);
      long timingError = abs((long)(elapsedTime - ecPumpDuration));
      
      Serial.println("🧪 MEGA EC Pump: ULTRA-PRECISE STOP!");
      Serial.println("   - Target Duration: " + String(ecPumpDuration) + " ms");
      Serial.println("   - Actual Duration: " + String(elapsedTime) + " ms");
      Serial.println("   - Timing Error: ±" + String(timingError) + " ms");
      Serial.println("   - Timing Accuracy: " + String(accuracy, 2) + "%");
      
      if (timingError <= 1) {
        Serial.println("🎯 PERFECT TIMING: Error ≤ 1ms (Ultra-Precise!)");
      } else if (timingError <= 5) {
        Serial.println("✅ EXCELLENT TIMING: Error ≤ 5ms (Very Good!)");
      } else if (timingError <= 10) {
        Serial.println("👍 GOOD TIMING: Error ≤ 10ms (Acceptable)");
      } else {
        Serial.println("⚠️ TIMING WARNING: Error > 10ms (Needs improvement)");
      }
      
      // ส่งสถานะกลับไป ESP32 พร้อมข้อมูลแม่นยำ
      Serial2.println("EC_PUMP_STOPPED:" + String(elapsedTime) + "," + String(ecPumpDuration) + "," + String(accuracy, 2) + "," + String(timingError));
    }
  }
  
  // ตรวจสอบปั๊ม PH
  if (phPumpRunning && phPumpDuration > 0 && phPumpStartTime > 0) {
    unsigned long elapsedTime;
    
    // ป้องกัน overflow
    if (currentTime >= phPumpStartTime) {
      elapsedTime = currentTime - phPumpStartTime;
    } else {
      elapsedTime = (0xFFFFFFFF - phPumpStartTime) + currentTime + 1;
    }
    
    if (elapsedTime >= phPumpDuration) {
      // ปิดปั๊ม PH ทันที (K6 = position 5)
      relayStates[5] = false; // K6 OFF
      if (relayActiveHigh[5]) {
        digitalWrite(relayPins[5], LOW);
      } else {
        digitalWrite(relayPins[5], HIGH);
      }
      
      phPumpRunning = false;
      
      // คำนวณความแม่นยำ
      float accuracy = 100.0 - (abs((long)(elapsedTime - phPumpDuration)) * 100.0 / phPumpDuration);
      Serial.println("🧪 MEGA PH Pump: ULTRA-PRECISE STOP after " + String(elapsedTime) + " ms (Target: " + String(phPumpDuration) + " ms)");
      Serial.println("⚡ Timing Accuracy: " + String(accuracy, 2) + "% (Error: ±" + String(abs((long)(elapsedTime - phPumpDuration))) + "ms)");
      
      // ส่งสถานะกลับไป ESP32
      Serial2.println("PH_PUMP_STOPPED:" + String(elapsedTime) + "," + String(phPumpDuration) + "," + String(accuracy, 2));
    }
  }
}

// ฟังก์ชันตรวจสอบและคำนวณอัตราการไหลของน้ำ
void checkFlowSensors() {
  // ตรวจสอบเซนเซอร์ที่ 1
  currentState1 = digitalRead(FLOW_SENSOR_1);
  if (lastState1 != currentState1 && currentState1 == LOW) {
    pulseCount1++;
  }
  lastState1 = currentState1;
  
  // ตรวจสอบเซนเซอร์ที่ 2
  currentState2 = digitalRead(FLOW_SENSOR_2);
  if (lastState2 != currentState2 && currentState2 == LOW) {
    pulseCount2++;
  }
  lastState2 = currentState2;
  
  // ตรวจสอบเซนเซอร์ที่ 3
  currentState3 = digitalRead(FLOW_SENSOR_3);
  if (lastState3 != currentState3 && currentState3 == LOW) {
    pulseCount3++;
  }
  lastState3 = currentState3;
  
  // คำนวณอัตราการไหลทุก 1 วินาที
  if ((millis() - oldTime) > 1000) {
    // คำนวณอัตราการไหล (ลิตร/นาที)
    flowRate1 = (pulseCount1 / calibrationFactor);
    flowRate2 = (pulseCount2 / calibrationFactor);
    flowRate3 = (pulseCount3 / calibrationFactor);
    
    // คำนวณปริมาณน้ำที่ไหลในช่วง 1 วินาที (มิลลิลิตร)
    float flowMilliLitres1 = (flowRate1 / 60) * 1000;
    float flowMilliLitres2 = (flowRate2 / 60) * 1000;
    float flowMilliLitres3 = (flowRate3 / 60) * 1000;
    
    // รวมปริมาณน้ำสะสม
    totalMilliLitres1 += flowMilliLitres1;
    totalMilliLitres2 += flowMilliLitres2;
    totalMilliLitres3 += flowMilliLitres3;
    
    // รีเซ็ตตัวแปรพัลส์
    pulseCount1 = 0;
    pulseCount2 = 0;
    pulseCount3 = 0;
    oldTime = millis();
    
    // แสดงข้อมูลเซนเซอร์วัดอัตราการไหลแบบสั้น (เฉพาะเมื่อมีการไหล)
    if (flowRate1 > 0 || flowRate2 > 0 || flowRate3 > 0) {
      Serial.print("Flow: ");
      Serial.print(flowRate1, 1); Serial.print(",");
      Serial.print(flowRate2, 1); Serial.print(",");
      Serial.print(flowRate3, 1); Serial.println(" L/min");
    }
  }
}

// ฟังก์ชันทดสอบการเชื่อมต่อกับเซ็นเซอร์ AC Power
void testACPowerSensor() {
  Serial.println("\n-- ทดสอบ AC Power Sensor (PZEM-004T) --");
  float voltage = pzem.voltage();
  
  if (isnan(voltage)) {
    acSensorConnected = false;
    Serial.println("❌ ไม่สามารถเชื่อมต่อกับ AC Power Sensor ได้");
  } else if (voltage >= 999999) {
    acSensorConnected = true;
    Serial.println("⚠️ พบ overflow (ไม่มีโหลด/เชื่อมต่อผิด)");
  } else {
    acSensorConnected = true;
    Serial.println("✅ เชื่อมต่อสำเร็จ แรงดัน: " + String(voltage) + " V");
  }
}

// ฟังก์ชันอ่านค่าจาก AC Power Sensor (PZEM-004T)
void readACPowerSensor() {
  if (!acSensorConnected) {
    // ลองเชื่อมต่อใหม่เมื่อไม่สามารถเชื่อมต่อได้
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt >= 10000) { // ลองเชื่อมต่อใหม่ทุก 10 วินาที
      lastReconnectAttempt = millis();
      testACPowerSensor();
    }
    return;
  }
  
  // อ่านค่าทั้งหมด
  float voltage = pzem.voltage();
  float current = pzem.current();
  float power = pzem.power();
  float energy = pzem.energy();
  float frequency = pzem.frequency();
  float pf = pzem.pf();
  
  // ตรวจสอบการเชื่อมต่อ
  if (isnan(voltage) && isnan(current) && isnan(power) && isnan(energy) && isnan(frequency) && isnan(pf)) {
    acSensorConnected = false;
    return;
  }
  
  // เก็บค่าโดยตรวจสอบ overflow
  acVoltage = (!isnan(voltage) && voltage < 999999) ? voltage : 0.0;
  acCurrent = (!isnan(current) && current < 999999) ? current : 0.0;
  acPower = (!isnan(power) && power < 999999) ? power : 0.0;
  acEnergy = (!isnan(energy) && energy < 999999) ? energy : 0.0;
  acFrequency = (!isnan(frequency) && frequency < 999999) ? frequency : 0.0;
  acPowerFactor = (!isnan(pf) && pf < 999999) ? pf : 0.0;
}

// ฟังก์ชันทดสอบการสื่อสารกับ ESP32
void testESP32Communication() {
  Serial.println("\n---- ทดสอบการสื่อสารกับ ESP32 ----");

  // ล้าง buffer เพื่อเริ่มต้นใหม่
  while (Serial2.available() > 0) {
    Serial2.read();
  }

  // ส่งคำขอทดสอบการสื่อสาร
  Serial2.println("MEGA_TEST");

  // รอการตอบกลับไม่เกิน 1 วินาที
  unsigned long startTime = millis();
  bool responseReceived = false;

  while (millis() - startTime < 1000 && !responseReceived) {
    if (Serial2.available() > 0) {
      String response = Serial2.readStringUntil('\n');
      Serial.println("ESP32 ตอบกลับ: " + response);

      // ✅ แก้ตรงนี้: ยอมรับได้ทั้ง "ESP32_OK" และ "ESP32_TEST"
      if (response.indexOf("ESP32_OK") >= 0 || response.indexOf("ESP32_TEST") >= 0) {
        communicationOK = true;
        responseReceived = true;
        Serial.println("✅ การสื่อสารกับ ESP32 ปกติ");
      }
    }
    delay(10);
  }

  if (!responseReceived) {
    communicationOK = false;
    Serial.println("❌ ไม่ได้รับการตอบกลับจาก ESP32");
  }
}

// ฟังก์ชันอ่านค่าจาก CO2 Sensor (ID 1)
void readCO2Sensor() {
  Serial.println("\n--- อ่านค่าจาก CO2 Sensor (ID 1) ---");

  // อ่าน 4 รีจิสเตอร์ (register 0 ถึง 3)
  uint8_t result = co2Sensor.readInputRegisters(0x0000, 4);

  if (result == co2Sensor.ku8MBSuccess) {
    // แสดงค่าดิบเพื่อ debug
    Serial.println("Raw values:");
    for (uint8_t i = 0; i < 4; i++) {
      Serial.print("Register ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(co2Sensor.getResponseBuffer(i));
    }

    // ถอดรหัสค่าจากรีจิสเตอร์:
    // - Register 1: Temperature (x10)
    // - Register 2: Humidity (x10)
    // - Register 3: CO2 (ppm)
    airTemp = co2Sensor.getResponseBuffer(1) / 10.0;
    airHumidity = co2Sensor.getResponseBuffer(2) / 10.0;
    co2Ppm = co2Sensor.getResponseBuffer(3);

    // แสดงค่าที่ถอดรหัสแล้ว
    Serial.print("🌡️ อุณหภูมิ: ");
    Serial.print(airTemp);
    Serial.println(" °C");

    Serial.print("💧 ความชื้น: ");
    Serial.print(airHumidity);
    Serial.println(" %RH");

    Serial.print("🫁 CO2: ");
    Serial.print(co2Ppm);
    Serial.println(" ppm");

    Serial.println("✅ อ่านข้อมูล CO2 Sensor สำเร็จ");

  } else {
    // ถ้าอ่านไม่ได้ให้เคลียร์ค่า
    airTemp = 0.0;
    airHumidity = 0.0;
    co2Ppm = 0;

    Serial.println("❌ ไม่สามารถอ่านข้อมูล CO2 Sensor ได้");
    Serial.print("Error Code: ");
    Serial.println(result);
  }
}

// ฟังก์ชันอ่านค่าจาก Light Sensor (ID 2)
void readLightSensor() {
  Serial.println("\n--- อ่านค่าจาก Light Sensor (ID 2) ---");
  uint8_t result = lightSensor.readInputRegisters(0x0001, 2);
  
  if (result == lightSensor.ku8MBSuccess) {
    uint16_t luxLow = lightSensor.getResponseBuffer(0);
    uint16_t luxHigh = lightSensor.getResponseBuffer(1);
    luxValue = ((uint32_t)luxHigh << 16) | luxLow;
    Serial.println("✅ อ่านข้อมูล Light Sensor สำเร็จ");
  } else {
    Serial.println("❌ ไม่สามารถอ่านข้อมูล Light Sensor ได้");
    Serial.print("Error Code: ");
    Serial.println(result);
  }
}

// ฟังก์ชันอ่านค่าจาก EC Sensor (ID 3)
void readECSensor() {
  Serial.println("\n--- อ่านค่าจาก EC Sensor (ID 3) ---");
  uint8_t result = ecSensor.readHoldingRegisters(0x00, 2);
  
  if (result == ecSensor.ku8MBSuccess) {
    uint16_t ecCalibrationRaw = ecSensor.getResponseBuffer(0);
    uint16_t ecValueRaw = ecSensor.getResponseBuffer(1);
    
    // แสดงค่าดิบเพื่อดีบัก
    Serial.print("EC Calibration Raw: ");
    Serial.println(ecCalibrationRaw);
    Serial.print("EC Value Raw: ");
    Serial.println(ecValueRaw);
    
    // ตรวจสอบว่าค่าเป็น 0 หรือค่าที่น้อยเกินไป (เช่น 1 ซึ่งจะกลายเป็น 0.1 เมื่อหารด้วย 10)
    if (ecValueRaw <= 1) {
      ecValue = 0.0;  // กำหนดค่าเป็น 0 ถ้ายังไม่มีการวัดที่แท้จริง
      Serial.println("ℹ️ กำหนดค่า EC เป็น 0 (ยังไม่มีการวัดที่แท้จริง)");
    } else {
      // ขั้นตอนที่ 1: แปลงค่า raw ตามช่วงของเซ็นเซอร์
      float rawEcValue;
      if (isEcSensorRange4400) {
        ecCalibration = ecCalibrationRaw / 10.0;
        rawEcValue = ecValueRaw / 10.0;  // ค่า raw ก่อน calibration
      } else {
        ecCalibration = ecCalibrationRaw;
        rawEcValue = ecValueRaw;         // ค่า raw ก่อน calibration
      }
      
      // ขั้นตอนที่ 2: ใช้สมการหลัก y = 15.968x - 53.913 (R² = 0.9973)
      ecValue = calibrateEC(rawEcValue);
      
      // แสดงผลการคำนวณ
      Serial.println("=== EC CALIBRATION ===");
      Serial.print("📊 Raw EC Input (x): ");
      Serial.println(rawEcValue, 3);
      Serial.print("🧮 สมการ: y = 15.968 × ");
      Serial.print(rawEcValue, 3);
      Serial.print(" + (-53.913)");
      Serial.print(" = ");
      Serial.println(ecValue, 2);
      Serial.print("✅ Final EC Value: ");
      Serial.print(ecValue, 2);
      Serial.println(" µS/cm");
      Serial.println("======================");
    }
  } else {
    Serial.println("❌ ไม่สามารถอ่านข้อมูล EC Sensor ได้");
    Serial.print("Error Code: ");
    Serial.println(result);
    
    // กำหนดค่าเป็น 0 เมื่อไม่สามารถอ่านได้
    ecValue = 0.0;
  }
}

void readPHSensor() {
  Serial.println("\n--- อ่านค่าจาก PH Sensor (ID 4) ---");
  
  // ตั้งค่าเริ่มต้นเป็น 0 ก่อนการอ่าน
  phValue = 0.0;
  waterTemp = 0.0;
  
  // อ่าน 3 registers เริ่มจาก register 0
  uint8_t result = phSensor.readHoldingRegisters(0x00, 3);
  
  if (result == phSensor.ku8MBSuccess) {
    uint16_t waterTempRaw = phSensor.getResponseBuffer(0); // อุณหภูมิน้ำ (register 0)
    uint16_t phValueRaw = phSensor.getResponseBuffer(1);   // ค่า pH (register 1)
    uint16_t idValue = phSensor.getResponseBuffer(2);      // ID (register 2)
    
    // แสดงค่าดิบเพื่อดีบัก
    Serial.print("Water Temp Raw: ");
    Serial.println(waterTempRaw);
    Serial.print("pH Raw: ");
    Serial.println(phValueRaw);
    Serial.print("ID Value: ");
    Serial.println(idValue);
    
    // ตรวจสอบว่าเซ็นเซอร์มีการวัดจริงหรือไม่ (ค่า raw ควรมากกว่า 10 สำหรับการวัดจริง)
    if (phValueRaw > 10) {  // ปรับค่านี้ตามความเหมาะสม
      phValue = phValueRaw / 10.0;
      Serial.print("🧪 ค่า pH: ");
      Serial.println(phValue);
    } else {
      Serial.println("ℹ️ ไม่พบการวัด pH ที่ถูกต้อง (ค่า raw ต่ำเกินไป)");
    }
    
    if (waterTempRaw > 10) {  // ปรับค่านี้ตามความเหมาะสม
      waterTemp = waterTempRaw / 10.0;
      Serial.print("🌡️ อุณหภูมิน้ำ: ");
      Serial.print(waterTemp);
      Serial.println(" °C");
    } else {
      Serial.println("ℹ️ ไม่พบการวัดอุณหภูมิน้ำที่ถูกต้อง (ค่า raw ต่ำเกินไป)");
    }
    
    Serial.println("✅ อ่านข้อมูล PH Sensor สำเร็จ");
  } else {
    Serial.println("❌ ไม่สามารถอ่านข้อมูล PH Sensor ได้");
    Serial.print("Error Code: ");
    Serial.println(result);
  }
}

// ฟังก์ชันอ่านค่าจาก Water Level Sensor (ต่อกับขา A0)
void readWaterLevel() {
  Serial.println("\n--- อ่านค่าจาก Water Level Sensor (A0) ---");
  waterDetected = digitalRead(WATER_LEVEL_PIN) == 1;
  Serial.println("✅ อ่านข้อมูล Water Level Sensor สำเร็จ");
}

// ฟังก์ชันแสดงค่าจากเซ็นเซอร์ทั้งหมด (แบบกระชับ)
void printAllValues() {
  // แสดงเฉพาะข้อมูลสำคัญในบรรทัดเดียว
  Serial.print("T:");
  Serial.print(airTemp, 1);
  Serial.print("C H:");
  Serial.print(airHumidity, 1);
  Serial.print("% CO2:");
  Serial.print(co2Ppm);
  Serial.print("ppm Light:");
  Serial.print(luxValue);
  Serial.print("Lux EC:");
  Serial.print(ecValue, 1);
  Serial.print(" PH:");
  Serial.print(phValue, 1);
  Serial.print(" WTemp:");
  Serial.print(waterTemp, 1);
  Serial.print("C");
  
  if (acSensorConnected) {
    Serial.print(" AC:");
    Serial.print(acVoltage, 1);
    Serial.print("V ");
    Serial.print(acPower, 1);
    Serial.print("W");
  }
  Serial.println();
}

// ฟังก์ชันรับคำสั่งจาก ESP32
void receiveCommandFromESP32() {
  if (Serial2.available() > 0) {
    String command = Serial2.readStringUntil('\n');
    command.trim(); // ตัดช่องว่างและ newline
    
    // แสดงคำสั่งที่ได้รับ
    Serial.println("\n---- ได้รับคำสั่งจาก ESP32 ----");
    Serial.println("Command: '" + command + "'");
    Serial.println("Length: " + String(command.length()));
    Serial.println("Raw bytes:");
    for (int i = 0; i < command.length(); i++) {
      Serial.print("0x");
      Serial.print(command[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    // ตรวจสอบข้อความคำสั่งพิเศษ
    if (command == "MEGA_TEST") {
      Serial2.println("MEGA_OK");
      return;
    }
    
    // === ULTRA-PRECISE TIMING COMMANDS ===
    // คำสั่งเริ่มจับเวลา Internal Fan: FAN_TIMING:K5,10,5
    if (command.startsWith("FAN_TIMING:K")) {
      int firstComma = command.indexOf(',');
      int secondComma = command.indexOf(',', firstComma + 1);
      
      if (firstComma > 0 && secondComma > 0) {
        String relayStr = command.substring(12, firstComma); // K5
        String delayOnStr = command.substring(firstComma + 1, secondComma); // 10
        String delayOffStr = command.substring(secondComma + 1); // 5
        
        int relayNum = relayStr.substring(1).toInt() - 1; // K5 -> 4 (index 4 = K5)
        unsigned long delayOn = delayOnStr.toInt() * 1000; // แปลงวินาทีเป็นมิลลิวินาที
        unsigned long delayOff = delayOffStr.toInt() * 1000; // แปลงวินาทีเป็นมิลลิวินาที
        
        // เริ่มต้น Internal Fan cycle
        fanCycleStartTime = millis();
        fanCycleState = false; // เริ่มด้วย OFF period
        fanDelayOn = delayOn;
        fanDelayOff = delayOff;
        fanRelayIndex = relayNum;
        fanCycleActive = true;
        
        Serial.println("🌀 MEGA INTERNAL FAN: Started Ultra-Precise Cycle Timer");
        Serial.println("   Relay: K" + String(relayNum + 1) + " (Pin " + String(relayPins[relayNum]) + ")");
        Serial.println("   Delay ON: " + String(delayOn/1000) + "s, Delay OFF: " + String(delayOff/1000) + "s");
        Serial.println("   Starting with OFF period");
        
        Serial2.println("FAN_TIMING_OK");
      }
      return;
    }
    
    // คำสั่งเริ่มจับเวลา EC Pump: PUMP_TIMING:EC,5000 หรือ PUMP_TIMING:EC,0 (หยุดทันที)
    if (command.startsWith("PUMP_TIMING:EC,")) {
      String durationStr = command.substring(15); // ตัด "PUMP_TIMING:EC," ออก
      ecPumpDuration = durationStr.toInt();
      
      // 🔥 FIX: ตรวจสอบว่าคำสั่งเป็นหยุดทันทีหรือไม่
      if (ecPumpDuration == 0) {
        // หยุดปั๊ม EC ทันที
        ecPumpRunning = false;
        ecPumpStartTime = 0;
        ecPumpDuration = 0;
        
        // ปิด relay K7 (EC Pump) ทันที
        relayStates[6] = false; // K7 OFF
        if (relayActiveHigh[6]) {
          digitalWrite(relayPins[6], LOW);
        } else {
          digitalWrite(relayPins[6], HIGH);
        }
        
        Serial.println("🛑 MEGA EC PUMP: STOPPED IMMEDIATELY");
        Serial.println("   Reason: Duration = 0 (EC too high)");
        Serial.println("✅ K7 (EC Pump) turned OFF immediately");
        
        Serial2.println("EC_PUMP_STOPPED:0,0,100.0,0");
        return;
      }
      
      // เริ่มจับเวลาปกติ
      ecPumpStartTime = millis();
      ecPumpRunning = true;
      
      // เปิด relay K7 (EC Pump) ทันที
      relayStates[6] = true; // K7 ON
      if (relayActiveHigh[6]) {
        digitalWrite(relayPins[6], HIGH);
      } else {
        digitalWrite(relayPins[6], LOW);
      }
      
      Serial.println("🧪 MEGA EC PUMP: Started Ultra-Precise Timer");
      Serial.println("   Duration: " + String(ecPumpDuration) + " ms");
      Serial.println("   Start Time: " + String(ecPumpStartTime) + " ms");
      Serial.println("✅ K7 (EC Pump) turned ON immediately");
      
      Serial2.println("EC_PUMP_TIMING_OK");
      return;
    }
    
    // คำสั่งเริ่มจับเวลา PH Pump: PUMP_TIMING:PH_ACID,3000 หรือ PUMP_TIMING:PH_BASE,3000 หรือ PUMP_TIMING:PH_ACID,0 (หยุดทันที)
    if (command.startsWith("PUMP_TIMING:PH_")) {
      int commaIndex = command.indexOf(',');
      if (commaIndex > 0) {
        String pumpType = command.substring(15, commaIndex); // ACID หรือ BASE
        String durationStr = command.substring(commaIndex + 1);
        phPumpDuration = durationStr.toInt();
        
        // 🔥 FIX: ตรวจสอบว่าคำสั่งเป็นหยุดทันทีหรือไม่
        if (phPumpDuration == 0) {
          // หยุดปั๊ม PH ทันที
          phPumpRunning = false;
          phPumpStartTime = 0;
          phPumpDuration = 0;
          
          // ปิด relay K6 (PH Pump) ทันที
          relayStates[5] = false; // K6 OFF
          if (relayActiveHigh[5]) {
            digitalWrite(relayPins[5], LOW);
          } else {
            digitalWrite(relayPins[5], HIGH);
          }
          
          Serial.println("🛑 MEGA PH " + pumpType + " PUMP: STOPPED IMMEDIATELY");
          Serial.println("   Reason: Duration = 0 (PH perfect)");
          Serial.println("✅ K6 (PH Pump) turned OFF immediately");
          
          Serial2.println("PH_PUMP_STOPPED:0,0,100.0,0");
          return;
        }
        
        // เริ่มจับเวลาปกติ
        phPumpStartTime = millis();
        phPumpRunning = true;
        
        // เปิด relay K6 (PH Pump) ทันที
        relayStates[5] = true; // K6 ON
        if (relayActiveHigh[5]) {
          digitalWrite(relayPins[5], HIGH);
        } else {
          digitalWrite(relayPins[5], LOW);
        }
        
        Serial.println("🧪 MEGA PH " + pumpType + " PUMP: Started Ultra-Precise Timer");
        Serial.println("   Duration: " + String(phPumpDuration) + " ms");
        Serial.println("   Start Time: " + String(phPumpStartTime) + " ms");
        Serial.println("✅ K6 (PH Pump) turned ON immediately");
        
        Serial2.println("PH_PUMP_TIMING_OK");
      }
      return;
    }

    // === RELAY CONTROL COMMANDS ===
    // รูปแบบ: RELAY:12345678 (1=ON, 0=OFF)
    if (command.startsWith("RELAY:")) {
      String relayPattern = command.substring(6); // ตัด "RELAY:" ออก
      Serial.println("📌 Relay pattern received: " + relayPattern);
      
      // ✅ อนุญาตให้ควบคุม relay อื่นๆ แต่ป้องกันเฉพาะ K6, K7
      if (ecPumpRunning || phPumpRunning) {
        Serial.println("🔒 Ultra-Precise pumps active - Protecting K6/K7 only (EC:" + String(ecPumpRunning) + ", PH:" + String(phPumpRunning) + ")");
      }
      
      if (relayPattern.length() == 8) {
        // ✅ ป้องกันเฉพาะ K6, K7 ขณะ Ultra-Precise Timing ทำงาน
        String protectedPattern = relayPattern;
        bool patternModified = false;
        
        // ถ้า EC Pump กำลังทำงาน ให้คงสถานะ K7 (ตำแหน่งที่ 6)
        if (ecPumpRunning && relayPattern.charAt(6) == '0') {
          protectedPattern.setCharAt(6, '1'); // บังคับให้ K7 เปิดต่อ
          Serial.println("🔒 Protected K7 (EC Pump) - kept ON during Ultra-Precise timing");
          patternModified = true;
        }
        
        // ถ้า PH Pump กำลังทำงาน ให้คงสถานะ K6 (ตำแหน่งที่ 5)
        if (phPumpRunning && relayPattern.charAt(5) == '0') {
          protectedPattern.setCharAt(5, '1'); // บังคับให้ K6 เปิดต่อ
          Serial.println("🔒 Protected K6 (PH Pump) - kept ON during Ultra-Precise timing");
          patternModified = true;
        }
        
        applyRelayCommand(protectedPattern);
        Serial2.println("RELAY_OK");
        Serial.println("✅ Relay command applied: " + protectedPattern);
        
        if (patternModified) {
          Serial.println("⚡ Pattern modified to protect Ultra-Precise pumps (K6/K7 only)");
        } else {
          Serial.println("✅ All relay states applied as requested");
        }
      } else {
        Serial2.println("RELAY_ERROR:INVALID_LENGTH");
        Serial.println("❌ Invalid relay command length: " + String(relayPattern.length()));
      }
      return;
    }
    
    // คำสั่งแสดงสถานะ relay
    if (command == "RELAY_STATUS") {
      printRelayStatus();
      String status = "RELAY_STATUS:";
      for (int i = 0; i < 8; i++) {
        status += relayStates[i] ? "1" : "0";
      }
      Serial2.println(status);
      return;
    }
    
    // === CONFIG COMMANDS ===
    
    // ตรวจสอบคำสั่งอื่นๆ (เดิม)
    if (command.indexOf("CONFIG:") >= 0) {
      // ตัวอย่างการปรับเปลี่ยนการตั้งค่า
      if (command.indexOf("EC_RANGE:4400") >= 0) {
        isEcSensorRange4400 = true;
        Serial2.println("CONFIG_OK:EC_RANGE_4400");
      } else if (command.indexOf("EC_RANGE:44000") >= 0) {
        isEcSensorRange4400 = false;
        Serial2.println("CONFIG_OK:EC_RANGE_44000");
      } else if (command.indexOf("RESET_ENERGY") >= 0) {
        pzem.resetEnergy();
        Serial2.println("CONFIG_OK:ENERGY_RESET");
      } else if (command.indexOf("RESET_FLOW") >= 0) {
        totalMilliLitres1 = 0;
        totalMilliLitres2 = 0;
        totalMilliLitres3 = 0;
        Serial2.println("CONFIG_OK:FLOW_RESET");
      }
      return;
    }
    
    // คำสั่งที่ไม่รู้จัก
    // ป้องกันการส่ง INVALID_FORMAT กลับไปเป็นลูปไม่รู้จบ
    if (command != "INVALID_FORMAT" && command != "UNKNOWN_COMMAND" && command != "DATA_RECEIVED") {
      Serial.println("⚠️ Unknown command received: " + command);
      Serial2.println("UNKNOWN_COMMAND");
    }
  }
}

// ฟังก์ชันส่งข้อมูลไปยัง ESP32
void sendDataToESP32() {
  // สร้าง JSON เพื่อส่งข้อมูลทั้งหมดในครั้งเดียว
  JsonDocument jsonDoc; // ใช้ JsonDocument แทน StaticJsonDocument
  
  // เพิ่ม marker เพื่อระบุว่านี่เป็นข้อมูลเซ็นเซอร์
  jsonDoc["msgType"] = "SENSOR_DATA";
  
  // ข้อมูล CO2 Sensor - ส่งค่าเต็ม
  jsonDoc["co2"] = co2Ppm;
  
  // ปรับรูปแบบให้มีทศนิยม 1 ตำแหน่ง
  jsonDoc["airTemp"] = round(airTemp * 10) / 10.0;
  jsonDoc["airHumidity"] = round(airHumidity * 10) / 10.0;
  
  // ข้อมูล Light Sensor - ส่งค่าเต็ม
  jsonDoc["light"] = luxValue;
  
  // ข้อมูล EC Sensor - ปรับรูปแบบให้มีทศนิยม 1 ตำแหน่ง
  jsonDoc["ec"] = round(ecValue * 10) / 10.0;
  
  // ข้อมูล PH Sensor - ปรับรูปแบบให้มีทศนิยม 1 ตำแหน่ง
  jsonDoc["ph"] = round(phValue * 10) / 10.0;
  jsonDoc["waterTemp"] = round(waterTemp * 10) / 10.0;
  
  // ข้อมูล Water Level
  jsonDoc["waterLevel"] = waterDetected ? 100 : 0;
  
  // เพิ่มข้อมูล AC Power Sensor
  if (acSensorConnected) {
    jsonDoc["acVoltage"] = acVoltage;
    jsonDoc["acCurrent"] = acCurrent;
    jsonDoc["acPower"] = acPower;
    jsonDoc["acEnergy"] = acEnergy;
    jsonDoc["acFrequency"] = acFrequency;
    jsonDoc["acPowerFactor"] = acPowerFactor;
  }
  
  // เพิ่มข้อมูลจากเซนเซอร์วัดอัตราการไหลของน้ำ (Flow Sensors)
  jsonDoc["flowSensor1_LPM"] = round(flowRate1 * 10) / 10.0;
  jsonDoc["flowSensor2_LPM"] = round(flowRate2 * 10) / 10.0;
  jsonDoc["flowSensor3_LPM"] = round(flowRate3 * 10) / 10.0;
  
  jsonDoc["flowSensor1_Liters"] = round((totalMilliLitres1 / 1000.0) * 100) / 100.0;
  jsonDoc["flowSensor2_Liters"] = round((totalMilliLitres2 / 1000.0) * 100) / 100.0;
  jsonDoc["flowSensor3_Liters"] = round((totalMilliLitres3 / 1000.0) * 100) / 100.0;
  
  // แปลง JSON เป็น String และส่งไปยัง ESP32
  serializeJson(jsonDoc, Serial2);
  Serial2.println();  // ปิดท้ายบรรทัดให้ ESP32 อ่านง่าย
  
  // แสดงข้อมูลที่ส่งไป ESP32 ครบถ้วน
  Serial.println("📤 === Data sent to ESP32 ===");
  Serial.print("CO2="); Serial.print(co2Ppm);
  Serial.print(" T="); Serial.print(airTemp, 1); Serial.print("C");
  Serial.print(" H="); Serial.print(airHumidity, 1); Serial.print("%");
  Serial.print(" Light="); Serial.print(luxValue);
  Serial.print(" EC="); Serial.print(ecValue, 1);
  Serial.print(" PH="); Serial.print(phValue, 1);
  Serial.print(" WTemp="); Serial.print(waterTemp, 1); Serial.print("C");
  Serial.print(" WLevel="); Serial.print(waterDetected ? "YES" : "NO");
  
  if (acSensorConnected) {
    Serial.print(" ACV="); Serial.print(acVoltage, 1);
    Serial.print("V ACP="); Serial.print(acPower, 1); Serial.print("W");
  }
  
  Serial.print(" Flow1="); Serial.print(flowRate1, 1);
  Serial.print(" Flow2="); Serial.print(flowRate2, 1);
  Serial.print(" Flow3="); Serial.print(flowRate3, 1); Serial.print("L/min");
  Serial.println();
  Serial.println("===============================");
}

// ===== RELAY CONTROL FUNCTIONS =====

/**
 * ฟังก์ชันเริ่มต้นระบบ Relay
 * ตั้งค่าขา OUTPUT และปิด relay ทั้งหมด
 * K1: Active High (HIGH = OFF, LOW = ON)
 * K2-K8: Active Low (HIGH = OFF, LOW = ON)
 */
void initRelays() {
  Serial.println("\n--- เริ่มต้นระบบ Relay Control ---");
  Serial.println("K1 (Light): Active High | K2-K8: Active Low");
  
  for (int i = 0; i < relayPinCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    
    if (relayActiveHigh[i]) {
      // K1 (Light): Active High - HIGH = OFF, LOW = ON
      digitalWrite(relayPins[i], LOW);
      Serial.print("Relay K"); 
      Serial.print(i + 1);
      Serial.print(" (Pin ");
      Serial.print(relayPins[i]);
      Serial.println(") = OFF (Active High)");
    } else {
      // K2-K8: Active Low - HIGH = OFF, LOW = ON
      digitalWrite(relayPins[i], HIGH);
      Serial.print("Relay K"); 
      Serial.print(i + 1);
      Serial.print(" (Pin ");
      Serial.print(relayPins[i]);
      Serial.println(") = OFF (Active Low)");
    }
    
    relayStates[i] = false;
  }
  Serial.println("✅ Relay system initialized\n");
}

/**
 * ฟังก์ชันควบคุม relay ตามคำสั่ง string
 * @param command String 8 ตัวอักษร เช่น "00110000"
 *                '1' = เปิด relay, '0' = ปิด relay
 */
void applyRelayCommand(String command) {
  // ตรวจสอบความยาวคำสั่ง
  int len = command.length();
  int n = min(len, relayPinCount);
  
  Serial.println("\n=== Apply Relay Command ===");
  Serial.println("Command: " + command);
  Serial.println("K1:Active High | K2-K8:Active Low");
  Serial.println("------------------------");
  
  for (int i = 0; i < n; i++) {
    char bitChar = command.charAt(i);
    bool shouldBeOn = (bitChar == '1');
    
    // ตรวจสอบว่าสถานะเปลี่ยนหรือไม่
    if (relayStates[i] != shouldBeOn) {
      relayStates[i] = shouldBeOn;
      
      if (shouldBeOn) {
        // เปิด relay
        if (relayActiveHigh[i]) {
          // K1 (Light): Active High - HIGH = ON
          digitalWrite(relayPins[i], HIGH);
          Serial.print("✅ Relay K");
          Serial.print(i + 1);
          Serial.print(" (Pin ");
          Serial.print(relayPins[i]);
          Serial.println(") = ON (Active High)");
        } else {
          // K2-K8: Active Low - LOW = ON
          digitalWrite(relayPins[i], LOW);
          Serial.print("✅ Relay K");
          Serial.print(i + 1);
          Serial.print(" (Pin ");
          Serial.print(relayPins[i]);
          Serial.println(") = ON (Active Low)");
        }
      } else {
        // ปิด relay
        if (relayActiveHigh[i]) {
          // K1 (Light): Active High - LOW = OFF
          digitalWrite(relayPins[i], LOW);
          Serial.print("❌ Relay K");
          Serial.print(i + 1);
          Serial.print(" (Pin ");
          Serial.print(relayPins[i]);
          Serial.println(") = OFF (Active High)");
        } else {
          // K2-K8: Active Low - HIGH = OFF
          digitalWrite(relayPins[i], HIGH);
          Serial.print("❌ Relay K");
          Serial.print(i + 1);
          Serial.print(" (Pin ");
          Serial.print(relayPins[i]);
          Serial.println(") = OFF (Active Low)");
        }
      }
    }
  }
  
  // บันทึกคำสั่งล่าสุด
  lastRelayCommand = command;
  Serial.println("========================\n");
}

/**
 * ฟังก์ชันแสดงสถานะ relay ทั้งหมด
 */
void printRelayStatus() {
  Serial.println("\n=== Relay Status ===");
  Serial.println("K1:Active High | K2-K8:Active Low");
  for (int i = 0; i < relayPinCount; i++) {
    Serial.print("K");
    Serial.print(i + 1);
    Serial.print(" (Pin ");
    Serial.print(relayPins[i]);
    Serial.print(") = ");
    Serial.print(relayStates[i] ? "ON" : "OFF");
    Serial.print(" (");
    Serial.print(relayActiveHigh[i] ? "Active High" : "Active Low");
    Serial.println(")");
  }
  Serial.println("Current pattern: " + lastRelayCommand);
  Serial.println("===================\n");
}

// ===== EC CALIBRATION FUNCTION (หลัก) =====

/**
 * ฟังก์ชันคำนวณค่า EC ที่แคลิเบรตแล้ว
 * สมการ: y = 15.968x - 53.913 (R² = 0.9973)
 * 
 * @param rawValue ค่า raw จากเซ็นเซอร์ EC (x)
 * @return ค่า EC ที่แคลิเบรตแล้ว (y) หน่วย µS/cm
 */
float calibrateEC(float rawValue) {
  // สมการหลัก: y = 15.968x - 53.913
  float calibratedValue = (EC_SLOPE * rawValue) + EC_INTERCEPT;
  
  // จำกัดช่วงค่าให้สมเหตุสมผล (0-5000 µS/cm)
  if (calibratedValue < 0.0) {
    calibratedValue = 0.0;    // ค่าต่ำสุด
  } else if (calibratedValue > 5000.0) {
    calibratedValue = 5000.0; // ค่าสูงสุด
  }
  
  return calibratedValue;
}