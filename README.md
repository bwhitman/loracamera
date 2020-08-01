# loracamera
transmit sound &amp; image over a lora link

`local`: esp-idf project for the device near a wifi link

`remote`: esp-idf project for the device out in a remote place 

`server`: python web server to handle file uploads. Just run `listen.py` and update your local IP from remote.cpp and local.cpp

This is set up as an esp-idf project instead of Arduino, althought I'm using the arduino component so that the camera package and LoRa package work fine. I have a lot of trouble with the Arduino IDE when there's more than one dev board connected. 

You will want `auth.h` to exist in the local/main/ folder, with 

```
#define WIFI_SSID your_ssid
#define WIFI_PASS your_password
```

