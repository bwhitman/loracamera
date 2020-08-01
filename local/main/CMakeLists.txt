
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wno-narrowing -Wno-write-strings")


idf_component_register(SRCS 
								local.cpp
								heltec/BMP180.cpp
								heltec/heltec.cpp
								heltec/lora/LoRa.cpp
								heltec/oled/OLEDDisplay.cpp
								heltec/oled/OLEDDisplayUi.cpp

                    INCLUDE_DIRS 
                    			heltec
                    			heltec/lora
                    			heltec/oled)