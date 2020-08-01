#define ESP32
#define WIFI_LoRa_32_V2
#define ARDUINO_RUNNING_CORE 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"
#include <pins_arduino.h>
#include <heltec.h>
#include <WiFi.h>
#include "HTTPClient.h"

// Wifi details & server for uploads
#include "auth.h"
const char * ssid = WIFI_SSID ;
const char * password = WIFI_PASS ;
const char * put_url = "http://192.168.0.3:8000/image_local.jpg";

#define BAND 915E6 // 868E6 433E6 915E6 
#define MAX_TRANSFER_BUFFER 100000 // on these boards we have another 160KB to work with if we need it, after this
#define LORA_TRANSFER_BUFFER 250 // has to be 254 or less
unsigned int counter = 0;
uint8_t * transfer_buffer;
uint8_t * lora_buffer;


void setup() { 
	Heltec.begin(true /*Display */, true /* LoRa */, true /* Serial */, true /* PABOOST */, BAND);
	// Set up WIFI 
	WiFi.begin(ssid, password);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
	}
	Serial.println("wifi online");
	// Malloc the transfer buffer 
	transfer_buffer = (uint8_t *) malloc(MAX_TRANSFER_BUFFER*sizeof(uint8_t));
	Serial.print("Heap after malloc: ");
	Serial.println(ESP.getFreeHeap());
	Heltec.display->init();
	Heltec.display->flipScreenVertically();  
	Heltec.display->setFont(ArialMT_Plain_10);
	delay(1500);
	Heltec.display->clear();
	Heltec.display->drawString(0, 0, "Heltec.LoRa Initial success!");
	Heltec.display->drawString(0, 10, "Wait for incoming data...");
	Heltec.display->display();
	delay(1000);

	// Configure LoRa for receiving
	LoRa.setTxPower(20,RF_PACONFIG_PASELECT_PABOOST);
	LoRa.setSignalBandwidth(250E3); 
	LoRa.setSpreadingFactor(7);
	LoRa.receive();
}

// Debug time taken to do things 
void print_time(char * title, uint16_t length, long time) {
	Serial.print(title);
	Serial.print(": ");
	Serial.print(length);
	Serial.print(" bytes in ");
	Serial.print((millis() - time) / 1000.0);
	Serial.print(" seconds. ");
	Serial.print((float)length / (float)(millis()-time) * 1000.0);
	Serial.println(" bytes/second.");
}

void put_to_server(uint8_t * buf, uint16_t length) {
	HTTPClient http;
	long time = millis();
	http.begin(put_url);
	http.addHeader("Content-Type", "image/jpg");
	http.addHeader("Content-Length", String(length));
	http.PUT(buf,length); 
	http.end();
	print_time("server", length, time);
}


uint16_t read_pointer = 0;
uint8_t read_flag = 0;
uint8_t packet_counter = 0;
void loop() {
	int packetSize = LoRa.parsePacket();
	if(packetSize) {
		Serial.print("got packet of ");
		Serial.println(packetSize);
	}
	if(packetSize == 2) {
		uint8_t a, b;
		a = LoRa.read();
		b = LoRa.read();
		if(a == 0x35 && b == 0xFA) {
			Serial.println("start packet!");
			read_flag = 1;
			packet_counter = 0;
			read_pointer = 0;
		} else if(a == 0x35 && b == 0xDA) {
			Serial.println("end packet!");
			if(read_flag) {
				read_flag = 0;
				// all done, do something with read_pointer bytes
				Serial.print("doing stuff with ");
				Serial.println(read_pointer);
				put_to_server(transfer_buffer, read_pointer);
			}
		} else {
			// rare case of the last packet being 2 but not our magic 2
			if(read_flag) {
				Serial.println("special case");
				// Skip the packet count magic stuff
				transfer_buffer[read_pointer++] = b;
				//transfer_buffer[read_pointer++] = b;
			}
		}
	} else if(packetSize > 0 && read_flag) {
		if(read_pointer + (packetSize-1) > MAX_TRANSFER_BUFFER) {
			Serial.println("overflow");
			read_flag = 0;
			read_pointer = 0;
			packet_counter = 0;
		} else {
			int available = LoRa.available();
			Serial.print("adding in ");
			Serial.println(packetSize-1);
			Serial.print("available is ");
			Serial.println(available);
			uint8_t packet_counter_reported = LoRa.read();
			Serial.print("reported packet #");
			Serial.print(packet_counter_reported);
			Serial.print(" vs expected ");
			Serial.println(packet_counter);
			for(uint8_t i=1;i < packetSize; i++) {
				uint8_t b = LoRa.read(); 
				transfer_buffer[read_pointer] = b;       
				read_pointer++;
			}
			Serial.print("length now ");
			Serial.println(read_pointer);
			packet_counter++;
		}
	}
	delay(10);
}




extern "C" {
	void loopTask(void *pvParameters)
	{
	    setup();
	    for(;;) {
	        micros(); //update overflow
	        loop();
	    }
	}

	void app_main()
	{
	    initArduino();
	    xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
	}

}