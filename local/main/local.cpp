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
#define REQUEST_TIMER 200
#define RESPONSE_TIMER 200
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
	lora_buffer = (uint8_t *) malloc(LORA_TRANSFER_BUFFER*sizeof(uint8_t));

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
		Serial.println("Sending request for transmission 0xF9 0xFC");
		LoRa.beginPacket();
		LoRa.write(lora_buffer, 2);
		LoRa.endPacket();
		delay(20);
		LoRa.receive();
		Serial.println("Waiting for rft response (0xF9 0xC2)");
		while(response_timer-- > 0) {
			int packetSize = LoRa.parsePacket();
			if(packetSize==3) {
				uint8_t a = LoRa.read();
				uint8_t b = LoRa.read();
				uint8_t c = LoRa.read();
				if(a == 0xF9 && b == 0xC2) {
					Serial.print("Received rft response of ");
					Serial.println(c);
					return c;
				}
			}
			delay(20);
		}
		Serial.println("Receive timer timed out");
	}
	Serial.println("Request timer timed out");
	return 0;
}


// Let's refactor this. Instead of a thing streaming all the time, we have local initiate the request
// Local sends a magic packet that asks
// (1) is there anything there, and how long is it? 
// (a) remote responds with # of packets available to send (0 is a number)
// (2) Ok, ready for packet #0! 
// (b) remote sends packet 0 on a loop until it gets an request for a new packet -- or something times out
// (3) Ok, ready for packet #1! 
// (c) remote sends packet 1...
// (4) Oh, i need packet 0 again..
// (d)  Sends packet 0

// That's it, right? 

uint16_t read_pointer = 0;
uint8_t read_flag = 0;
uint8_t packet_counter = 0;

void loop() {
	uint8_t packets = request_transmission();
	if(packets) {
		Serial.print("Got back these many packets: ");
		Serial.println(packets);
		for(uint8_t i=0;i<packets;i++) {
			uint8_t request_timer = REQUEST_TIMER;
			uint8_t ok = 0;
			while(request_timer-- > 0 && !ok) {
				Serial.print("requesting packet ");
				Serial.println(i);
				delay(100); // delay 100ms between packet requests
				request_packet(i);
				uint8_t response_timer = RESPONSE_TIMER;
				while(response_timer-- > 0 && !ok) {
					int packetSize = LoRa.parsePacket();
					if(packetSize) {
						uint8_t packet_number = LoRa.read();
						if(packet_number == 0xF9 && packetSize < 4) {
							Serial.println("discarding magic packet");
							delay(20);
						} else {
							Serial.print("got response, this is packet # ");
							Serial.println(packet_number);
							Serial.print("packet size was ");
							Serial.println(packetSize);
							if(packet_number == i) {
								for(uint j=0;j<packetSize-1;j++) { 
									uint8_t b = LoRa.read(); 
									transfer_buffer[read_pointer++] = b;    
								}
								ok = 1;
								Serial.println("Right packet received");
							} else {
								Serial.println("packet numbers don't match somehow");
							}
						}
					}
					delay(20);
				}
				if(!ok) Serial.println("response timer timeout");
				delay(20);
			}
			if(!ok) Serial.println("request timer timeout");
		}
		Serial.print("Loaded all packets. Read this many bytes ");
		Serial.println(read_pointer);
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