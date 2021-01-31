sudo cp wiringMqtt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl restart wiringMqtt
sudo systemctl status wiringMqtt

