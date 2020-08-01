#include "heltec.h"
#include <Adafruit_VC0706.h>
#include <WiFi.h>
#include "HTTPClient.h"

// Wifi details & server for uploads
const char * ssid = "Crow";
const char * password = "rosaline";
const char * put_url = "http://192.168.0.3:8000/image_sender.jpg";

#define BAND 915E6 // 868E6 433E6 915E6 
#define MAX_TRANSFER_BUFFER 100000 // on these boards we have another 160KB to work with if we need it, after this
#define LORA_TRANSFER_BUFFER 250 // has to be 254 or less
unsigned int counter = 0;
Adafruit_VC0706 * cam;
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
  
  // Set up the camera.  
  // purple on 17, grey on 23, blue on 5V, white on GND
  Serial2.begin(38400,SERIAL_8N1, 23, 17);
  cam = new Adafruit_VC0706(&Serial2);
  cam->reset();
  Serial.println(cam->getVersion());
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

void put_to_lora(uint8_t *buf, uint16_t length) {
  uint16_t idx = 0;    
  // First send a "hey, a picture!!!" packet of a known length and content
  lora_buffer[0] = 0x35;
  lora_buffer[1] = 0xFA;
  LoRa.beginPacket();
  LoRa.write(lora_buffer, 2);
  LoRa.endPacket();
  delay(500);
  // Now the whole dingus  
  uint8_t packet_counter = 0;
  long time = millis();
  while(length > 0) {
    uint8_t bytesToSend = min((uint16_t)LORA_TRANSFER_BUFFER, length);
    lora_buffer[0] = packet_counter;
    for(int j=0;j<bytesToSend;j++) {
      lora_buffer[j+1] = buf[idx++]; 
    }
    LoRa.beginPacket();
    LoRa.write(lora_buffer, bytesToSend+1);  
    LoRa.endPacket();
    packet_counter++;
    length -= bytesToSend;      
    delay(500); // slow it down to let the receiver catch it 
  }
  print_time("LoRa  ", idx, time);

  // Now send an all done packet
  lora_buffer[0] = 0x35;
  lora_buffer[1] = 0xDA;
  LoRa.beginPacket();
  LoRa.write(lora_buffer, 2);
  LoRa.endPacket();

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

void loop() { 
  if(cam->takePicture()) {
    delay(100);
    uint16_t jpglen = cam->frameLength();
    long time = millis();
    // The camera has a tiny (100 byte) buffer for transfers, so we read a bit at a time into ESP ram
    // Takes about 20 seconds, but keeping this at 38400 as it's fiddly and that is not the slow part!
    uint16_t idx = 0;
    uint16_t content_length = jpglen; // save this off
    if(content_length < MAX_TRANSFER_BUFFER) {
      while(jpglen > 0) {
        uint8_t bytesToRead = min((uint16_t)64, jpglen);
        uint8_t * camera_buffer = cam->readPicture(bytesToRead);
        for(int j=0;j<bytesToRead;j++) {
          transfer_buffer[idx++] = camera_buffer[j];
        }
        jpglen -= bytesToRead;      
      }
      print_time("camera", content_length, time);
      put_to_server(transfer_buffer, content_length);
      put_to_lora(transfer_buffer, content_length);
    } else {
      Serial.println("buffer too big");
    }
    // Restart the camera
    cam->resumeVideo();
  } else {
    Serial.println("Couldn't take pic");
  }

  
  Heltec.display->clear();
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->drawString(0, 0, "Sending packet: ");
  Heltec.display->drawString(90, 0, String(counter));
  Heltec.display->display();

  // send packet
  /*
  LoRa.beginPacket();
  LoRa.setTxPower(20,RF_PACONFIG_PASELECT_PABOOST);
  LoRa.print("hello ");
  LoRa.print(counter);
  LoRa.endPacket();
  */
  counter++;
  
  delay(30000);
}
