cmake_minimum_required(VERSION 3.0.0)
project(wiringMqtt VERSION 0.1.0)

include(CTest)
enable_testing()

add_executable(wiringMqtt  src/Main.cpp  src/Sys.cpp src/Timer.cpp   src/NanoAkka.cpp src/MqttPaho.cpp ) 

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
set(GCC_FLAGS "-DRPI")
include(CPack)

add_definitions(${GCC_FLAGS})
include_directories(src ../Common ../ArduinoJson/src ../paho.mqtt.c/src ../nanoAkka/main/ ../nanoAkka/components/wifi ../paho.mqtt.c/src)
target_link_libraries(wiringMqtt -lpthread -L/home/lieven/workspace/paho.mqtt.c/build/output/ -l:libpaho-mqtt3c.a -L/home/lieven/workspace/Common/Debug/ -lCommon -lrt  )
