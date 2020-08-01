

#ifndef _HELTEC_H_
#define _HELTEC_H_


#include <Wire.h>
#include "oled/SSD1306Wire.h"
	#include <SPI.h>
	#include "lora/LoRa.h"


class Heltec_ESP32 {

 public:
    Heltec_ESP32();
	~Heltec_ESP32();

    void begin(bool DisplayEnable=true, bool LoRaEnable=true, bool SerialEnable=true, bool PABOOST=true, long BAND=470E6);
    LoRaClass LoRa;

    SSD1306Wire *display;

/*wifi kit 32 and WiFi LoRa 32(V1) do not have vext*/
    void VextON(void);
    void VextOFF(void);
};

extern Heltec_ESP32 Heltec;



#endif
