#define ESP32
#define WIFI_LoRa_32_V2
#define ARDUINO_RUNNING_CORE 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Arduino.h"
#include <pins_arduino.h>
#include <heltec.h>
#include <Adafruit_VC0706.h>
#include "driver/i2s.h"

#define BAND 915E6 // 868E6 433E6 915E6 

#define MAX_TRANSFER_BUFFER 125000 
#define LORA_TRANSFER_BUFFER 250 
#define SEND_REPEAT 5

#define SAMPLE_RATE 11025
#define AUDIO_SECONDS 5
#define GAIN_FACTOR 2 
#define AUDIO_BUF_SIZE (SAMPLE_RATE*(AUDIO_SECONDS+1))
#define AUDIO_FRAME_SIZE 1024

// Pins
#define CAMERATX_BOARDRX 23
#define CAMERARX_BOARDTX 17
#define I2S_BCK 13
#define I2S_LRC 12
#define I2S_DIN 39 // DOUT from I2S mic


Adafruit_VC0706 * cam;
uint8_t transfer_buffer[MAX_TRANSFER_BUFFER];
uint8_t lora_buffer[LORA_TRANSFER_BUFFER];

uint32_t content_length = 0;
uint32_t audio_pointer = 0;

uint8_t current_transfer = 0;
long lora_time = 0;

// unsigned int packing functions
uint8_t u0(uint16_t in) { return (in & 0xFF); }
uint8_t u1(uint16_t in) { return (in >> 8); }
uint16_t u(uint8_t a, uint8_t b) { uint16_t n = b; n = n << 8; n = n | a; return n; }


//i2s configuration
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
     .sample_rate = SAMPLE_RATE,
     .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
     .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, 
     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 4,
     .dma_buf_len = AUDIO_FRAME_SIZE,
     .use_apll = false,
     .tx_desc_auto_clear = false,
     .fixed_mclk = 0,   //Interrupt level 1
    };
    
i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK,
    .ws_io_num = I2S_LRC, 
    .data_out_num = -1, // not used //27, // this is DIN 
    .data_in_num = I2S_DIN,
};

void setup_i2s(void) {
	pinMode(I2S_DIN, INPUT);
	pinMode(I2S_BCK, OUTPUT);
	pinMode(I2S_LRC, OUTPUT);
	i2s_driver_install((i2s_port_t)I2S_NUM_0, &i2s_config, 0, NULL);
	i2s_set_pin((i2s_port_t)I2S_NUM_0, &pin_config);
	i2s_set_sample_rates((i2s_port_t)I2S_NUM_0, SAMPLE_RATE);
	i2s_start(I2S_NUM_0);
}

void read_audio(void) {
	uint32_t bytes_read = 0;
	int32_t small_buf[AUDIO_FRAME_SIZE/4];
	i2s_read((i2s_port_t)I2S_NUM_0, small_buf, AUDIO_FRAME_SIZE, &bytes_read, portMAX_DELAY );
	for(uint32_t i=0;i<(bytes_read/4);i++) { 
		int16_t sample = (int16_t) (small_buf[i] >> (12-GAIN_FACTOR));
		transfer_buffer[audio_pointer++] = (sample & 0xFF);
		transfer_buffer[audio_pointer++] = (sample >> 8);
		if(audio_pointer-1 == MAX_TRANSFER_BUFFER) audio_pointer = 0;
	}
}

void setup() {
	setup_i2s();
	Heltec.begin(true /*Display */, true /* LoRa */, true /* Serial */, true /* PABOOST */, BAND);

	// Set up the camera.  
	Serial1.begin(38400,SERIAL_8N1, CAMERATX_BOARDRX, CAMERARX_BOARDTX);
	cam = new Adafruit_VC0706(&Serial1);
	int t = cam->reset();
	log_d("reset said %d\n", t);
	delay(100);
	char * version = cam->getVersion();

	log_d("-- %s --\n", version);
	delay(100);
	cam->setImageSize(VC0706_320x240); 
	delay(100);

	printf("I have %d free heap\n", ESP.getFreeHeap());

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


}

// Debug time taken to do things 
void print_time(char * title, uint32_t length, long time) {
	printf("%s: %d bytes in %2.2fs. %2.2f bytes/s.\n", 
		title, length, (millis() - time) / 1000.0, (float)length / (float)(millis()-time) * 1000.0  );
}


void recordAudio() {
	printf("Recording %d seconds of audio\n", AUDIO_SECONDS);
	long start_time = millis();
	audio_pointer = 0;
	while(millis() - start_time < (AUDIO_SECONDS*1000)) {
		read_audio();
	}
	content_length = audio_pointer;
	print_time("microphone", content_length, start_time);
}

// Take a picture and load it into transfer bffer
void takePicture() {
	printf("Taking pic\n");
	if(cam->takePicture()) {
		delay(100);
		uint32_t jpglen = cam->frameLength();
		delay(100);
		printf("len of frame is %d\n", jpglen);
		long pic_time = millis();
		// The camera has a tiny (100 byte) buffer for transfers, so we read a bit at a time into ESP ram
		// Takes about 20 seconds, but keeping this at 38400 as it's fiddly and that is not the slow part!
		uint32_t idx = 0;
		content_length = jpglen; // save this off
		while(jpglen > 0) {
			uint8_t bytesToRead = min((uint32_t)64, jpglen);
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


void loop() { 
	// Check for inbound packets and take a pic once in a while
	int packetSize = LoRa.parsePacket();
	if(packetSize == 2) {
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		delay(20);
		if(a==0xF9 && (b==0xFC || b==0xFD)) { // request for start
			// Both of these take 5-10 seconds to do
			// Both load into transfer_buffer, both set content_length

			if(b==0xFC) { // take picture
				takePicture();
			} else { // record audio
				recordAudio();
			}

			uint16_t packets = content_length / LORA_TRANSFER_BUFFER;
			if(content_length % LORA_TRANSFER_BUFFER != 0) packets++;
			printf("RFT received. Content length is %d; buffer count %d. Packets to send is %d\n", 
				content_length, LORA_TRANSFER_BUFFER, packets);
			lora_buffer[0] = 0xF9;
			lora_buffer[1] = 0xC2;
			lora_buffer[2] = u0(packets);
			lora_buffer[3] = u1(packets);
			uint8_t send_repeat = SEND_REPEAT;
			while(send_repeat-- > 0) {
				LoRa.beginPacket();
				LoRa.write(lora_buffer, 4);
				LoRa.endPacket();
				delay(20);
			}

			LoRa.receive();
			lora_time = millis();
		}
	} else if (packetSize == 4) { 
		uint8_t a = LoRa.read();
		uint8_t b = LoRa.read();
		uint8_t c = LoRa.read();
		uint8_t d = LoRa.read();
		uint16_t cd = u(c,d);
		if(a==0xF9 && b==0xFE) { // request for packet
			current_transfer = 1; // we are in a transfer
			uint32_t byte_start = LORA_TRANSFER_BUFFER * cd;
			uint8_t bytes_to_send = LORA_TRANSFER_BUFFER;

			if(cd == (content_length / LORA_TRANSFER_BUFFER)) { // if it's last packet
				bytes_to_send = content_length % LORA_TRANSFER_BUFFER;
				current_transfer = 0; // We're done
			}
			printf("Packet %d was requested. byte_start is %d. content_length %d, active %d\n", 
				cd, byte_start, content_length, current_transfer);

			lora_buffer[0] = u0(cd);
			lora_buffer[1] = u1(cd);
			// +2 is for the uint16_t header that has the packet # in it
			for(uint32_t i=byte_start;i<byte_start+bytes_to_send;i++) {
				lora_buffer[i-byte_start+2] = transfer_buffer[i];
			}
			delay(100);

			LoRa.beginPacket();
			LoRa.write(lora_buffer, bytes_to_send+2);
			LoRa.endPacket();
			delay(20);
			if(current_transfer == 0) print_time("lora", content_length, lora_time);
			LoRa.receive();
		} 
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