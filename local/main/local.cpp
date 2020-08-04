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
#define MAX_TRANSFER_BUFFER 65535 // on these boards we have another 200KB to work with if we need it, after this
#define LORA_TRANSFER_BUFFER 250 // has to be 254 or less
#define REQUEST_TIMER 200
#define RESPONSE_TIMER 200
#define CHECK_EVERY_MS 45000
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

	// Malloc the transfer buffers
	transfer_buffer = (uint8_t *) malloc(MAX_TRANSFER_BUFFER*sizeof(uint8_t));
	lora_buffer = (uint8_t *) malloc(LORA_TRANSFER_BUFFER*sizeof(uint8_t));

	printf("After malloc I have %d free heap\n", ESP.getFreeHeap());

	Heltec.display->init();
	Heltec.display->flipScreenVertically();  
	Heltec.display->setFont(ArialMT_Plain_10);
	delay(1500);
	Heltec.display->clear();
	Heltec.display->display();
	delay(1000);

	// Configure LoRa
	LoRa.setTxPower(20,RF_PACONFIG_PASELECT_PABOOST);
	LoRa.setSignalBandwidth(250E3); 
	LoRa.setSpreadingFactor(7);
	LoRa.receive();
}

// Debug time taken to do things 
void print_time(char * title, uint16_t length, long time) {
	printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
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

// request a particular packet
void request_packet(uint8_t packet_number) {
	lora_buffer[0] = 0xF9;
	lora_buffer[1] = 0xFD;
	lora_buffer[2] = packet_number;
	LoRa.beginPacket();
	LoRa.write(lora_buffer, 3);
	LoRa.endPacket();
	delay(20);
	LoRa.receive();
}

// say, how many packets you got for me
uint8_t request_transmission() {
	uint8_t request_timer = REQUEST_TIMER;
	while(request_timer-- > 0) {
		uint8_t response_timer = RESPONSE_TIMER;
		lora_buffer[0] = 0xF9;
		lora_buffer[1] = 0xFC;
		printf("Sending request for transmission...\n");
		LoRa.beginPacket();
		LoRa.write(lora_buffer, 2);
		LoRa.endPacket();
		delay(20);
		LoRa.receive();
		while(response_timer-- > 0) {
			int packetSize = LoRa.parsePacket();
			if(packetSize==3) {
				uint8_t a = LoRa.read();
				uint8_t b = LoRa.read();
				uint8_t c = LoRa.read();
				if(a == 0xF9 && b == 0xC2) {
					printf("Receiving rft response, %d packets\n", c);
					return c;
				}
			}
			delay(20);
		}
		printf("Receive timer timed out\n");
	}
	printf("Request timer timed out\n");
	return 0;
}


long lastCheckTime = 0;
void loop() {
	uint8_t packets = 0;
	if(millis() - lastCheckTime > CHECK_EVERY_MS) {
		lastCheckTime = millis();
		packets = request_transmission();
	}
	if(packets) {
		uint16_t read_pointer = 0;
		long time = millis();
		printf("Got back %d packets from the rft\n", packets);
		for(uint8_t i=0;i<packets;i++) {
			uint8_t request_timer = REQUEST_TIMER;
			uint8_t ok = 0;
			while(request_timer-- > 0 && !ok) {
				delay(100); // delay 100ms between packet requests
				request_packet(i);
				uint8_t response_timer = RESPONSE_TIMER;
				while(response_timer-- > 0 && !ok) {
					int packetSize = LoRa.parsePacket();
					if(packetSize) {
						uint8_t packet_number = LoRa.read();
						if(packet_number == 0xF9 && packetSize < 4) {
							printf("got magic packet, discarding\n");
							delay(20);
						} else {
							printf("Response: packet #%d (asked for %d), size %d\n", packet_number, i, packetSize);
							if(packet_number == i) {
								for(uint j=0;j<packetSize-1;j++) { 
									uint8_t b = LoRa.read(); 
									transfer_buffer[read_pointer++] = b;    
								}
								ok = 1;
							} else {
								printf("Mismatched packet\n");
							}
						}
					}
					delay(20);
				}
				if(!ok) printf("response timer timeout\n");
				delay(20);
			}
			if(!ok) printf("request timer timeout\n");
		}
		printf("Loaded all packets, total %d\n", read_pointer);
		print_time("lora", read_pointer, time);
		put_to_server(transfer_buffer, read_pointer);

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