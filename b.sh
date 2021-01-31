cd ../limero/linux/build
make
cd ../../../wiringMqtt/build
touch ../src/Main.cpp
make
cd ..
./build/wiringMqtt