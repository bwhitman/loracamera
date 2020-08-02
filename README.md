# loracamera

transmit sound &amp; image over a 915MHz LoRa link to a base station near your wifi. Currently set up to a send a frame from a serial TTL camera, but can be adapted for most data you want to send using LoRa directly. 

I'm using two of the [Heltec WiFi LoRa 32 v2](https://heltec.org/project/wifi-lora-32/), readily available on amazon etc. I'm able to get about 1km in range in a dense wooded place from this setup.

The camera I'm using is [this one from Adafruit](https://www.adafruit.com/product/613), but I'm looking for better options. The library is very fiddly, I had to slow down a bunch of serial calls to get it going on an ESP32, and the camera quality is low. 

`local`: esp-idf project for the device near a wifi link 

`remote`: esp-idf project for the device out in a remote place 

`server`: python web server to handle file uploads. Just run `listen.py` and update your local IP from local.cpp.

This is set up as an esp-idf project instead of Arduino, althought I'm using the Arduino component so that the camera package and Heltec LoRa package work fine. I have a lot of trouble with the Arduino IDE when there's more than one dev board connected and would rather work in a terminal anyway. I'm sure it's easy to port this back to Arduino if you want, it's simple enough.

You will want `auth.h` to exist in the local/main/ folder, with 

```
#define WIFI_SSID your_ssid
#define WIFI_PASS your_password
```
