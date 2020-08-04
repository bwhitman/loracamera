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
#define PIC_EVERY_MS 45000
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
	printf("After malloc I have %d free heap\n", ESP.getFreeHeap());

	// Set up the OLED
	Heltec.display->init();
	Heltec.display->flipScreenVertically();  
	Heltec.display->setFont(ArialMT_Plain_10);
	delay(1500);
	Heltec.display->clear();  
	Heltec.display->display();
	delay(1000);

	LoRa.setTxPower(20,RF_PACONFIG_PASELECT_PABOOST);
	LoRa.setSignalBandwidth(250E3); 
	LoRa.setSpreadingFactor(7);

	// take a first picture
	lastPicTime = millis();
	takePicture();
}

// Debug time taken to do things 
void print_time(char * title, uint16_t length, long time) {
	printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}




// Take a picture and load it into transfer bffer
void takePicture() {
	printf("Taking pic\n");
	if(cam->takePicture()) {
		delay(100);
		uint16_t jpglen = cam->frameLength();
		delay(100);
		printf("len of frame is %d\n", jpglen);
		long pic_time = millis();
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
				log_e("problem reading data from camera -- read %d bytes", idx);
				delay(100);
			}
		}
		print_time("camera", content_length, pic_time);
		// Restart the camera
		cam->resumeVideo();
	} else {
		log_e("Couldn't take pic");
	}
}
uint8_t current_transfer = 0;
long lora_time = 0;

void loop() { 
	// Check for inbound packets and take a pic once in a while
	int packetSize = LoRa.parsePacket();
	if(packetSize == 2) {
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		delay(20);
		if(a==0xF9 && b==0xFC) {
			delay(100);
			uint8_t packets = content_length / LORA_TRANSFER_BUFFER;
			if(content_length % LORA_TRANSFER_BUFFER != 0) packets++;
			printf("RFT received. Content length is %d; buffer count %d. Packets to send is %d\n", 
				content_length, LORA_TRANSFER_BUFFER, packets);
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
			lora_time = millis();
		}
	} else if (packetSize == 3) { 
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		uint8_t c = LoRa.read();
		if(a==0xF9 && b==0xFD) { // request for packet
			current_transfer = 1; // tell the camera to stop taking pictures
			uint16_t byte_start = LORA_TRANSFER_BUFFER * c;
			uint8_t bytes_to_send = LORA_TRANSFER_BUFFER;

			if(c == (content_length / LORA_TRANSFER_BUFFER)) { // if it's last packet
				bytes_to_send = content_length % LORA_TRANSFER_BUFFER;
				current_transfer = 0; // We're done
			}
			printf("Packet %d was requested. byte_start is %d. content_length %d, active %d\n", 
				c, byte_start, content_length, current_transfer);

			lora_buffer[0] = c;
			for(uint16_t i=byte_start;i<byte_start+bytes_to_send;i++) {
				lora_buffer[i-byte_start+1] = transfer_buffer[i];
			}
			delay(100);

			LoRa.beginPacket();
			LoRa.write(lora_buffer, bytes_to_send+1);
			LoRa.endPacket();
			delay(20);
			if(current_transfer == 0) print_time("lora", content_length, lora_time);
			LoRa.receive();
		} 
	}
	if(!current_transfer && (millis() - lastPicTime > PIC_EVERY_MS)) {
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