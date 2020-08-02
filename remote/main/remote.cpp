#define ESP32
#define WIFI_LoRa_32_V2
#define ARDUINO_RUNNING_CORE 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"
#include <pins_arduino.h>
#include <heltec.h>
#include <Adafruit_VC0706.h>

#define BAND 915E6 // 868E6 433E6 915E6 
#define MAX_TRANSFER_BUFFER 100000 // on these boards we have another 160KB to work with if we need it, after this
#define LORA_TRANSFER_BUFFER 250 // has to be 254 or less
#define PIC_EVERY_MS 60000
#define SEND_REPEAT 5

void takePicture();

unsigned int counter = 0;
Adafruit_VC0706 * cam;
uint8_t * transfer_buffer;
uint8_t * lora_buffer;
uint16_t content_length = 0;
long lastPicTime = 0;

void setup() {
	Heltec.begin(true /*Display */, true /* LoRa */, true /* Serial */, true /* PABOOST */, BAND);

	// Set up the camera.  
	// purple on 17, grey on 23, blue on 5V, white on GN
	Serial1.begin(38400,SERIAL_8N1, 23, 17);
	cam = new Adafruit_VC0706(&Serial1);
	int t = cam->reset();
	log_d("reset said %d\n", t);
	delay(100);
	char * version = cam->getVersion();

	log_d("-- %s --\n", version);
	delay(100);
	cam->setImageSize(VC0706_320x240); 
	delay(100);

	// Malloc the transfer buffer 
	transfer_buffer = (uint8_t *) malloc(MAX_TRANSFER_BUFFER*sizeof(uint8_t));
	lora_buffer = (uint8_t *) malloc(LORA_TRANSFER_BUFFER*sizeof(uint8_t));
	Serial.print("Heap after malloc: ");
	Serial.println(ESP.getFreeHeap());

	// Set up the OLED
	Heltec.display->init();
	Heltec.display->flipScreenVertically();  
	Heltec.display->setFont(ArialMT_Plain_10);
	delay(1500);
	Heltec.display->clear();  
	Heltec.display->drawString(0, 0, "Heltec.LoRa Initial success!");
	Heltec.display->display();
	delay(1000);

	// Configure LoRa for sending
	LoRa.setTxPower(20,RF_PACONFIG_PASELECT_PABOOST);
	LoRa.setSignalBandwidth(250E3); 
	LoRa.setSpreadingFactor(7);

	// take a first picture
	lastPicTime = millis();
	takePicture();
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



// Take a picture and load it into transfer bffer
void takePicture() {
	Serial.println("taking pic");
	if(cam->takePicture()) {
		delay(100);
		uint16_t jpglen = cam->frameLength();
		delay(100);
		log_d("jpglen is %d\n", jpglen);
		long time = millis();
		// The camera has a tiny (100 byte) buffer for transfers, so we read a bit at a time into ESP ram
		// Takes about 20 seconds, but keeping this at 38400 as it's fiddly and that is not the slow part!
		uint16_t idx = 0;
		content_length = jpglen; // save this off
		while(jpglen > 0) {
			uint8_t bytesToRead = min((uint16_t)64, jpglen);
			uint8_t * camera_buffer = cam->readPicture(bytesToRead);
			if(camera_buffer) {
				for(int j=0;j<bytesToRead;j++) {
					transfer_buffer[idx++] = camera_buffer[j];
				}
				jpglen -= bytesToRead;      
			} else {
				log_e("problem reading data from camera -- read %d bytes\n", idx);
				delay(100);
			}
		}
		print_time("camera", content_length, time);
		// Restart the camera
		cam->resumeVideo();
	} else {
		Serial.println("Couldn't take pic");
	}
}

void loop() { 
	// my loop is ... check for inbound packets a lot... but if you don't get one for 60s, take a picture
	int packetSize = LoRa.parsePacket();
	if(packetSize == 2) {
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		delay(20);
		if(a==0xF9 && b==0xFC) {
			Serial.println("rft received");
			delay(100);
			uint8_t packets = content_length / LORA_TRANSFER_BUFFER;
			if(content_length % LORA_TRANSFER_BUFFER != 0) packets++;
			Serial.print("Content len / buffer count is ");
			Serial.println(packets);
			lora_buffer[0] = 0xF9;
			lora_buffer[1] = 0xC2;
			lora_buffer[2] = packets;
			uint8_t send_repeat = SEND_REPEAT;
			while(send_repeat-- > 0) {
				LoRa.beginPacket();
				LoRa.write(lora_buffer, 3);
				LoRa.endPacket();
				delay(20);
			}
			LoRa.receive();
		}
	} else if (packetSize == 3) { // request for packet
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		uint8_t c = LoRa.read();
		if(a==0xF9 && b==0xFD) {
			Serial.print("packet was requested: ");
			Serial.println(c);
			uint16_t byte_start = LORA_TRANSFER_BUFFER * c;
			uint8_t bytes_to_send = LORA_TRANSFER_BUFFER;
			if(c == (content_length / LORA_TRANSFER_BUFFER) - 1) { // if it's last packet
				Serial.print("last packet so ");
				bytes_to_send = content_length % LORA_TRANSFER_BUFFER;
				Serial.println(bytes_to_send);
			}
			// Send the "which packet is this" guy
			Serial.println("Sending packet");
			lora_buffer[0] = c;
			for(uint16_t i=byte_start;i<byte_start+bytes_to_send;i++) {
				//printf("putting byte %d in lora_buffer position %d, c is %d bytes_to_send %d i is %d\n", 
				//	transfer_buffer[i], i-byte_start+1, c, bytes_to_send, i);
				lora_buffer[i-byte_start+1] = transfer_buffer[i];
			}
			Serial.println("filled send buffer");
			delay(100);

			uint8_t send_repeat = 1;
			while(send_repeat-- > 0) {
				LoRa.beginPacket();
				LoRa.write(lora_buffer, bytes_to_send+1);
				LoRa.endPacket();
				Serial.println("send actual packet");
				delay(20);
			}
			Serial.println("waiting around for more commands");
			LoRa.receive();
		} 
	}
	if(millis() - lastPicTime > PIC_EVERY_MS) {
		lastPicTime = millis();
		takePicture();
	}
	delay(20);

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