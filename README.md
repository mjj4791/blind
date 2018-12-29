# Blind
An ESP8266 based blind controller, to control (for example) a window blind for a skylight.

![test](https://github.com/mjj4791/blind/blob/master/img/Main.png)

## Features

 - Webinterface
	 - Control
		 - open
		 - close
		 - stop
	 - [Configuration](https://github.com/mjj4791/blind/blob/master/img/config.png)
	 - [Detailed Device state](https://github.com/mjj4791/blind/blob/master/img/State.png)
 - MQTT integration
	 - publish state
	 - receive commands
	 - LWT-support
 - Syslog & serial logging


## Hardware

 - [x] 2 * IKEA plisse curtain ([ikea](https://www.ikea.com/nl/nl/catalog/products/90369507/))
 - [x] ESP8266 / WeMos D1 mini ([banggood](https://www.banggood.com/search/wemos-d1-mini.html))
 - [x] H Bridge motor driver (L298n, [banggood](https://www.banggood.com/buy/l298n-motor-driver.html))
 - [x] Micro switches (detect open/closed position)
 - [x] 3D printer timing belt (for example: [this](https://www.banggood.com/10M-2GT-Timing-Belt-20-Teeth-GT2-Aluminium-Pulley-For-3D-Printer-CNC-RepRap-p-1081310.html?rmmds=detail-left-hotproducts__1&HotRecToken=CgEwEAIaAklWIgJQRCgB&cur_warehouse=CN) )
 - [x] 12V DC Motor with 5mm shaft ([dx.com](https://www.dx.com/p/zhaoyao-200rpm-5mm-shaft-dia-pernament-magnetic-dc-12v-gearbox-geared-motor-white-2083509#.XCedVvlKiMo))
 - [x] 12V DC power adapter
 - [x] various bits and pieces (bolts, screws, wood, fishing wire)

## Connections / Schematic
**ESP8266**:

 - D1 --> Closed switch
 - D2 --> Open switch
 - D3 --> L298N IN1
 - D4 --> L298N IN2
 - D5 --> do_open switch
 - D6 --> do_close switch
 - 5V --> L298N 5V out
 - 0V --> L298N 0V

**Switches**: 
All switches connect to ground.

**L298N**:
 - IN1 --> ESP D3
 - IN2 --> ESP D4
 - ENA --> bridged
 - 12V --> Poweradapter
 - 5V --> ESP 5V
 - 0V --> ESP 0V
 - M1 --> to dc motor
 - M2 --> to dc motor

**Switches**:
 - Closed: switch closed when blind is fully closed (outputs 0V when switch is closed)
 - Open: switch closed when blind is fully open  (outputs 0V when switch is closed)
 - do_open: switch to open the blind (0V when switch is momentarily closed)
 - do_close: switch to close the blind (0V when switch is momentarily closed)
 
 
 
 
