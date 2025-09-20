# Persistent Storage System Guide

## ภาพรวมระบบ

ระบบ Persistent Storage ได้รับการออกแบบมาเพื่อแก้ไขปัญหาการสูญหายของคำสั่งควบคุมเมื่อเกิดไฟดับหรือรีเซ็ตบอร์ด โดยจะบันทึกคำสั่งล่าสุดและกู้คืนสถานะการทำงานอัตโนมัติ

## คุณสมบัติหลัก

### 🔄 ESP32-S3 - NVS Flash Storage
- **บันทึกคำสั่งล่าสุด**: เก็บคำสั่ง JSON ที่ได้รับจาก MQTT ลงใน NVS Flash
- **กู้คืนอัตโนมัติ**: โหลดและประมวลผลคำสั่งล่าสุดเมื่อเริ่มต้นระบบ
- **ความปลอดภัย**: ใช้ Preferences library สำหรับการจัดการ NVS อย่างปลอดภัย

### 💾 Arduino Mega2560 - EEPROM Storage
- **บันทึกสถานะ Relay**: เก็บสถานะ K1-K8 ลงใน EEPROM
- **บันทึกคำสั่งสำคัญ**: เก็บคำสั่ง RELAY, PUMP_TIMING, FAN_TIMING
- **Validation**: ใช้ flag ตรวจสอบความถูกต้องของข้อมูล

## การทำงานของระบบ

### 1. การบันทึกข้อมูล (Save Process)

#### ESP32-S3:
```cpp
// บันทึกคำสั่งล่าสุดเมื่อได้รับจาก MQTT
void callback(char* topic, byte* payload, unsigned int length) {
    // ... parse JSON ...
    
    // 💾 บันทึกคำสั่งล่าสุดลงใน NVS Flash Memory
    saveLastCommand(receivedMessage);
    
    // ... process command ...
}
```

#### Arduino Mega2560:
```cpp
// บันทึกสถานะ relay เมื่อมีการเปลี่ยนแปลง
void applyRelayCommand(String command) {
    // ... apply relay states ...
    
    // บันทึกสถานะ relay ลง EEPROM
    saveRelayStatesToEEPROM();
}

// บันทึกคำสั่งสำคัญที่ได้รับจาก ESP32
void receiveCommandFromESP32() {
    // ... receive command ...
    
    // บันทึกคำสั่งล่าสุดลง EEPROM (เฉพาะคำสั่งสำคัญ)
    if (command.startsWith("RELAY:") || command.startsWith("PUMP_TIMING:") || command.startsWith("FAN_TIMING:")) {
        saveLastCommandToEEPROM(command);
    }
}
```

### 2. การกู้คืนข้อมูล (Recovery Process)

#### ESP32-S3:
```cpp
void setup() {
    // ... system initialization ...
    
    // โหลดคำสั่งล่าสุดจาก NVS Flash Memory
    loadLastCommand();
    
    // ... complete setup ...
    
    // ใช้คำสั่งล่าสุดที่บันทึกไว้ (ถ้ามี) หลังจากระบบพร้อมทำงานแล้ว
    if (shouldRestoreLastCommand) {
        Serial.println("⏳ PERSISTENT: Waiting 3 seconds before applying last command...");
        delay(3000); // รอให้ระบบเสถียรก่อนใช้คำสั่ง
        applyLastCommand();
    }
}
```

#### Arduino Mega2560:
```cpp
void setup() {
    // ... system initialization ...
    
    // โหลดข้อมูลจาก EEPROM
    loadRelayStatesFromEEPROM();
    
    // ... complete setup ...
    
    // ใช้คำสั่งล่าสุดที่บันทึกไว้ (ถ้ามี)
    applyLastCommand();
}
```

## ข้อมูลที่บันทึก

### ESP32-S3 NVS Storage:
| Key | ข้อมูล | ขนาด |
|-----|--------|------|
| `lastCommand` | คำสั่ง JSON ล่าสุดจาก MQTT | ~8KB |

### Arduino Mega2560 EEPROM:
| Address | ข้อมูล | ขนาด |
|---------|--------|------|
| 0-7 | สถานะ Relay K1-K8 | 8 bytes |
| 10-109 | คำสั่งล่าสุด | 100 bytes |
| 110 | Validation Flag (0xAA) | 1 byte |

## ข้อความ Debug

### ESP32-S3 Messages:
```
💾 PERSISTENT: Saved last command to NVS: {"commands":[...]}
🔄 PERSISTENT: Loaded last command from NVS: {"commands":[...]}
⏳ PERSISTENT: Waiting 3 seconds before applying last command...
🔄 PERSISTENT: Applying last saved command...
✅ PERSISTENT: Restored command result: SUCCESS
```

### Arduino Mega2560 Messages:
```
💾 PERSISTENT: Saved relay states to EEPROM
💾 PERSISTENT: Saved last command to EEPROM: RELAY:10101010
🔄 PERSISTENT: Loading relay states from EEPROM
✅ PERSISTENT: Restored K1 = ON
❌ PERSISTENT: Restored K2 = OFF
✅ PERSISTENT: Restored relay pattern: 10101010
🔄 PERSISTENT: Applying last saved command: RELAY:10101010
✅ PERSISTENT: Applied relay command: 10101010
```

## การทดสอบระบบ

### 1. ทดสอบการบันทึก:
1. ส่งคำสั่งผ่าน MQTT: `{"commands":[{"device":"light","state":true}]}`
2. ตรวจสอบข้อความ: `💾 PERSISTENT: Saved last command to NVS`
3. ตรวจสอบข้อความ: `💾 PERSISTENT: Saved relay states to EEPROM`

### 2. ทดสอบการกู้คืน:
1. รีเซ็ตหรือปิด-เปิดไฟ
2. ตรวจสอบข้อความ: `🔄 PERSISTENT: Loaded last command from NVS`
3. ตรวจสอบข้อความ: `🔄 PERSISTENT: Loading relay states from EEPROM`
4. ตรวจสอบว่าอุปกรณ์กลับมาทำงานตามสถานะเดิม

### 3. ทดสอบความแม่นยำ:
1. ตั้งค่าอุปกรณ์หลายตัว: Light ON, Fan ON, CO2 OFF
2. รีเซ็ตระบบ
3. ตรวจสอบว่าสถานะทุกอุปกรณ์กลับมาเหมือนเดิม

## ข้อจำกัดและข้อควรระวัง

### ESP32-S3:
- **ขนาดคำสั่ง**: จำกัดที่ ~8KB (ขึ้นอยู่กับ NVS partition)
- **Timing Commands**: คำสั่ง PUMP_TIMING และ FAN_TIMING ต้องประสานงานกับ Mega
- **Recovery Delay**: รอ 3 วินาทีก่อนใช้คำสั่งเพื่อให้ระบบเสถียร

### Arduino Mega2560:
- **ขนาด EEPROM**: จำกัดที่ 1024 bytes (Arduino Mega)
- **คำสั่งล่าสุด**: จำกัดที่ 99 ตัวอักษร
- **Timing Commands**: ไม่กู้คืนคำสั่งจับเวลาแบบแม่นยำ (ต้องการ ESP32)

## การบำรุงรักษา

### ล้างข้อมูล EEPROM (Arduino Mega2560):
```cpp
// เขียนค่า 0xFF ทั้งหมดเพื่อล้าง EEPROM
for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0xFF);
}
```

### ล้างข้อมูล NVS (ESP32-S3):
```cpp
preferences.begin("hydroponics", false);
preferences.clear();
preferences.end();
```

## สรุป

ระบบ Persistent Storage ช่วยให้ระบบไฮโดรโปนิกส์สามารถกู้คืนสถานะการทำงานได้อัตโนมัติหลังจากเกิดปัญหาไฟดับหรือรีเซ็ต โดย:

1. **ESP32-S3** บันทึกคำสั่งควบคุมล่าสุดใน NVS Flash
2. **Arduino Mega2560** บันทึกสถานะ relay และคำสั่งสำคัญใน EEPROM
3. **ระบบกู้คืน** ทำงานอัตโนมัติเมื่อเริ่มต้นระบบ

ทำให้ระบบมีความน่าเชื่อถือสูงและลดความเสี่ยงจากการหยุดทำงานของอุปกรณ์เมื่อเกิดปัญหาไฟฟ้า
