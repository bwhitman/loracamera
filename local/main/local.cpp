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
#define SERVER_URL "http://192.168.0.3:8000"
#include "auth.h"
const char * ssid = WIFI_SSID ;
const char * password = WIFI_PASS ;

#define BAND 915E6 // 868E6 433E6 915E6 
#define MAX_TRANSFER_BUFFER 125000 
#define LORA_TRANSFER_BUFFER 250 
#define REQUEST_TIMER 200
#define RESPONSE_TIMER 200
#define CHECK_EVERY_MS 10000
// The max the remote device will take to acquire image / audio
#define MAX_RFT_RESPONSE_MS 30000
#define AUDIO_SECONDS 5

unsigned int counter = 0;

#define AUDIO 1
#define PICTURE 0

long lastCheckTime = 0;
uint8_t content_type = PICTURE;

// TODO -- bank this so that we can have more room
uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER];


// unsigned int packing functions
uint8_t u0(uint16_t in) { return (in & 0xFF); }
uint8_t u1(uint16_t in) { return (in >> 8); }
uint16_t u(uint8_t a, uint8_t b) { uint16_t n = b; n = n << 8; n = n | a; return n; }

void setup() { 
	Heltec.begin(true /*Display */, true /* LoRa */, true /* Serial */, true /* PABOOST */, BAND);
	// Set up WIFI 
	WiFi.begin(ssid, password);
	while(WiFi.status() != WL_CONNECTED) {
		delay(500);
	}


	printf("I have %d free heap\n", ESP.getFreeHeap());

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
void print_time(char * title, uint32_t length, long time) {
	printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


void put_to_server(uint8_t * buf, uint32_t length) {
	char put_url[255];
	HTTPClient http;
	long time = millis();

	if(content_type == AUDIO) {
		sprintf(put_url, "%s/audio.raw", SERVER_URL);
		http.begin(put_url);
		http.addHeader("Content-Type", "audio/L8");
	} else {
		sprintf(put_url, "%s/image.jpg", SERVER_URL);
		http.begin(put_url);
		http.addHeader("Content-Type", "image/jpg");		
	}
	http.addHeader("Content-Length", String(length));
	http.PUT(buf,length); 
	http.end();
	print_time("server", length, time);
}

// request a particular packet
void request_packet(uint16_t packet_number) {
	lora_buffer[0] = 0xF9;
	lora_buffer[1] = 0xFE;
	lora_buffer[2] = u0(packet_number);
	lora_buffer[3] = u1(packet_number);
	LoRa.beginPacket();
	LoRa.write(lora_buffer, 4);
	LoRa.endPacket();
	delay(20);
	LoRa.receive();
}

// say, how many packets you got for me
// type == 0 for image, 1 for audio
uint16_t request_transmission() {
	uint8_t request_timer = REQUEST_TIMER;
	while(request_timer-- > 0) {
		lora_buffer[0] = 0xF9;
		lora_buffer[1] = 0xFC + content_type;
		printf("Sending request for transmission of type %d...\n", content_type);
		LoRa.beginPacket();
		LoRa.write(lora_buffer, 2);
		LoRa.endPacket();

		// Wait for either audio or picture to capture
		long wfr_time = millis();
		LoRa.receive();
		while(millis() - wfr_time < MAX_RFT_RESPONSE_MS) {
			int packetSize = LoRa.parsePacket();
			if(packetSize==4) {
				uint8_t a = LoRa.read();
				uint8_t b = LoRa.read();
				uint8_t c = LoRa.read();
				uint8_t d = LoRa.read();
				uint16_t cd = u(c, d);
				if(a == 0xF9 && b == 0xC2) {
					printf("Receiving rft response, %d packets\n", cd);
					return cd;
				}
			}
			delay(20);
		}
		printf("Receive timer timed out\n");
	}
	printf("Request timer timed out\n");
	return 0;
}

void update_status(uint16_t packet_number, uint16_t packets, int rssi) {
	char message[255];
	sprintf(message, "%d / %d [%2.2f%%]", 
		packet_number, packets, ((float)packet_number/packets)*100.0f);

	Heltec.display->clear();
	Heltec.display->setFont(ArialMT_Plain_10);
	Heltec.display->drawString(0, 0, message);
	printf(message);
	sprintf(message, " rssi: %d\n", rssi);
	Heltec.display->drawString(0, 15, message);
	Heltec.display->display();
	printf(message);

}

void handle_packets(uint16_t packets) {
	uint32_t read_pointer = 0;
	long time = millis();
	printf("Got back %d packets from the rft\n", packets);
	for(uint16_t i=0;i<packets;i++) {
		uint8_t request_timer = REQUEST_TIMER;
		uint8_t ok = 0;
		while(request_timer-- > 0 && !ok) {
			delay(100); // delay 100ms between packet requests
			request_packet(i);
			uint8_t response_timer = RESPONSE_TIMER;
			while(response_timer-- > 0 && !ok) {
				int packetSize = LoRa.parsePacket();
				if(packetSize) {
					uint8_t a = LoRa.read();
					uint8_t b = LoRa.read();
					uint16_t packet_number = u(a, b);
					if(a == 0xF9 && packetSize < 5) {
						printf("got magic packet, discarding\n");
						delay(20);
					} else {
						if(packet_number == i) {
							for(uint j=0;j<packetSize-2;j++) { 
								uint8_t b = LoRa.read(); 
								transfer_buffer[read_pointer++] = b;    
							}
							ok = 1;
							update_status(packet_number, packets, LoRa.packetRssi());
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


void loop() {
	uint16_t packets = 0;
	if(millis() - lastCheckTime > CHECK_EVERY_MS) {
		lastCheckTime = millis();
		packets = request_transmission();
	}
	if(packets) {
		printf("if packets! %d\n", packets);
		handle_packets(packets);
		lastCheckTime = millis();
		if(content_type == AUDIO) { content_type = PICTURE; } else { content_type = AUDIO; } // flip type
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