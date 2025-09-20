# 🌱 ระบบควบคุมไฮโดรโปนิกส์ - ภาพรวมระบบ

## 📋 สถาปัตยกรรมระบบ

### **2 บอร์ดหลัก:**

#### **1. Arduino Mega2560 - Sensor Hub & Relay Controller**
- **หน้าที่**: อ่านเซ็นเซอร์และควบคุม relay
- **การเชื่อมต่อ**: Serial2 กับ ESP32, Modbus RTU กับเซ็นเซอร์
- **Relay 8 ช่อง**: K1-K8 (K1=Active High, K2-K8=Active Low)

#### **2. ESP32-S3 - IoT Controller & Logic Processor**
- **หน้าที่**: รับคำสั่ง MQTT, ประมวลผล control logic, ส่งคำสั่งไป Mega
- **การเชื่อมต่อ**: MQTT, Serial2 กับ Mega, BeeNeXT Display
- **การสื่อสาร**: ESP32 GPIO17(TX2)→Mega RX2(Pin17), ESP32 GPIO18(RX2)→Mega TX2(Pin16)

---

## 🔌 การเชื่อมต่อ Hardware

### **Arduino Mega2560:**
```
Serial1 (Pin 18,19) → Modbus RTU Sensors
Serial2 (Pin 16,17) → ESP32 Communication  
Serial3 (Pin 14,15) → PZEM-004T AC Power Meter
A0 (Pin 54) → Water Level Sensor
D22-24 → Flow Sensors (3 channels)
D26-33 → Relay Control (K1-K8)
```

### **ESP32-S3:**
```
GPIO17 (TX2) → Mega RX2 (Pin 17)
GPIO18 (RX2) → Mega TX2 (Pin 16)
WiFi → MQTT Broker
UART → BeeNeXT Display
```

---

## 🌡️ เซ็นเซอร์ที่ใช้

### **Modbus RTU Sensors (Serial1):**
- **CO2 Sensor (ID 1)**: อุณหภูมิ, ความชื้น, CO2 ppm
- **Light Sensor (ID 2)**: ความสว่าง (Lux)
- **EC Sensor (ID 3)**: ค่า EC (µS/cm) - สมการ y=15.968x-53.913
- **PH Sensor (ID 4)**: ค่า pH, อุณหภูมิน้ำ

### **Digital Sensors:**
- **Water Level (A0)**: ระดับน้ำ (Digital)
- **Flow Sensors (D22-24)**: อัตราการไหล 3 ช่อง (L/min)
- **AC Power Meter (Serial3)**: แรงดัน, กระแส, กำลัง, พลังงาน

---

## 🎛️ ระบบควบคุม

### **1. หลอดไฟ (K1 - Light Control)**
- **เงื่อนไข**: เวลา + ความสว่าง < threshold
- **การควบคุม**: เปิดเมื่อแสงน้อย, ปิดเมื่อแสงเพียงพอ

### **2. พัดลมระบายอากาศ (K3 - External Fan)**
- **เงื่อนไข**: อุณหภูมิอากาศ ≥ 28°C (เปิด), ≤ 25°C (ปิด)
- **การควบคุม**: Hysteresis control

### **3. พัดลมภายใน (K5 - Internal Fan)**
- **เงื่อนไข**: เวลา 8:00-20:00 น.
- **การควบคุม**: Cycling 30 นาที ON / 15 นาที OFF

### **4. CO2 (K8 - CO2 Control)**
- **เงื่อนไข**: CO2 < 400 ppm + พัดลมปิด
- **การควบคุม**: ป้องกันการรั่วไหล

### **5. ทำความเย็น (K2 - Cooling)**
- **เงื่อนไข**: อุณหภูมิน้ำ ≥ 25°C (เปิด), ≤ 22°C (ปิด)
- **การควบคุม**: Hysteresis control

### **6. EC/PH Control (K6, K7)**
- **เงื่อนไข**: ทุก 8 ชั่วโมง
- **การควบคุม**: Ultra-Precise Timing System
- **EC Pump (K7)**: เปิดตาม E_error × KofEC
- **PH Pump (K6)**: เปิดตาม |PH_error| × KofPH

---

## ⚡ Ultra-Precise Timing System

### **ปัญหาที่แก้ไข:**
- ปั๊มปิดช้า 52 วินาที
- การจับเวลาไม่แม่นยำ
- Stop sequence ซับซ้อน

### **วิธีแก้ไขใหม่:**
- **ปิดทันทีเมื่อถึงเวลา**: ไม่ต้องผ่าน stop sequence
- **ตรวจสอบ timer 2 ครั้งต่อลูป**: ความแม่นยำสูงสุด
- **แสดงความแม่นยำเป็น %**: วัดผลได้ชัดเจน
- **ESP32 รีเซ็ต status หลังส่งคำสั่ง**: ให้ Mega จัดการทั้งหมด
- **ป้องกันการปิดพร้อมกันด้วย delay 100ms**: ง่ายและเร็ว

### **เป้าหมาย:**
- ความแม่นยำ ± 5ms (>99.5%)
- ปิดทันทีตามเวลาที่กำหนดไม่ช้า

---

## 📡 การสื่อสาร

### **ESP32 → Mega2560:**
```
RELAY:xxxxxxxx          # ควบคุม relay 8 ช่อง
PUMP_TIMING:EC,5000     # เปิดปั๊ม EC 5 วินาที
PUMP_TIMING:PH_ACID,3000 # เปิดปั๊ม PH กรด 3 วินาที
FAN_TIMING:K5,10,5      # พัดลม K5 ON 10s, OFF 5s
DATA_REQUEST            # ขอข้อมูลเซ็นเซอร์
```

### **Mega2560 → ESP32:**
```
JSON Sensor Data        # ข้อมูลเซ็นเซอร์ทั้งหมด
RELAY_OK               # ยืนยันการควบคุม relay
EC_PUMP_STOPPED        # แจ้งปิดปั๊ม EC
PH_PUMP_STOPPED        # แจ้งปิดปั๊ม PH
FAN_CYCLE_STATE        # สถานะพัดลม
```

---

## 🔄 Data Flow

### **1. การอ่านข้อมูล:**
```
Mega2560 → อ่านเซ็นเซอร์ทุก 1 วินาที
Mega2560 → ส่งข้อมูลไป ESP32 ทุก 2 วินาที
```

### **2. การควบคุม:**
```
ESP32 → รับคำสั่ง MQTT
ESP32 → ประมวลผล control logic
ESP32 → ส่งคำสั่งไป Mega2560
Mega2560 → ควบคุม relay ตามคำสั่ง
```

### **3. การแสดงผล:**
```
ESP32 → ส่งข้อมูลไป MQTT
ESP32 → ส่งข้อมูลไป BeeNeXT Display
Mega2560 → แสดงสถานะบน Serial Monitor
```

---

## 🎯 คุณสมบัติพิเศษ

### **1. Non-blocking Control:**
- ระบบทำงานแบบ non-blocking
- ไม่มีการ delay ที่ขัดขวางการทำงาน

### **2. Hysteresis Control:**
- ป้องกันการเปิด-ปิดบ่อยเกินไป
- คงสถานะระหว่างช่วงอุณหภูมิ

### **3. Cycling Control:**
- พัดลมภายในทำงานแบบ cycling
- เปิด-ปิดสลับกันตามเวลาที่กำหนด

### **4. Time-based Control:**
- ควบคุมตามเวลาของวัน
- รองรับการตั้งค่าเวลาเปิด-ปิด

### **5. Batch Processing:**
- ประมวลผลคำสั่งหลายตัวพร้อมกัน
- ลดการส่งคำสั่งซ้ำซ้อน

---

## 🚀 ข้อดีของระบบ

### **1. ความแม่นยำสูง:**
- Ultra-Precise Timing System
- ความแม่นยำ ± 5ms

### **2. ความเสถียร:**
- ระบบ non-blocking
- การจัดการ error ที่ดี

### **3. ความยืดหยุ่น:**
- ปรับแต่งได้ผ่าน MQTT
- รองรับการขยายระบบ

### **4. การทำงานอิสระ:**
- EC และ PH ทำงานแยกกัน
- แต่ละระบบไม่รบกวนกัน

### **5. การตรวจสอบ:**
- Real-time monitoring
- การแจ้งเตือนเมื่อผิดปกติ

---

## 📊 สรุป

ระบบควบคุมไฮโดรโปนิกส์นี้เป็นระบบที่สมบูรณ์แบบสำหรับการปลูกพืชไร้ดิน โดยใช้:

- **Arduino Mega2560** เป็น Sensor Hub และ Relay Controller
- **ESP32-S3** เป็น IoT Controller และ Logic Processor
- **Ultra-Precise Timing System** สำหรับความแม่นยำสูงสุด
- **Non-blocking Control** สำหรับการทำงานที่เสถียร
- **MQTT Communication** สำหรับการควบคุมระยะไกล

ระบบพร้อมใช้งานจริงและสามารถปรับแต่งได้ตามความต้องการ! 🌱✨
