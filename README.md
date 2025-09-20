; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:megaatmega2560]
platform = atmelavr
board = megaatmega2560
framework = arduino
lib_deps = 
	mandulaj/PZEM-004T-v30@^1.1.2
	4-20ma/ModbusMaster@^2.0.1
	bblanchon/ArduinoJson@^7.4.1
	https://github.com/mandulaj/PZEM-004T-v30
upload_port = COM10
monitor_port = COM10
monitor_speed = 115200
