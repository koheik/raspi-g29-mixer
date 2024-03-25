PROGRAM=raspi-g29-mixer
LDFLAG=-lusb-1.0 -pthread -ljsoncpp

ifndef CFLAGS
	ifeq ($(TARGET),Debug)
		CFLAGS=-Wall -Wextra -g
	else
		CFLAGS=-Wall -Wextra -O2
	endif
endif

.PHONY: all clean

$(PROGRAM): usb-proxy.o host-raw-gadget.o device-libusb.o proxy.o misc.o input-device.o
	g++ usb-proxy.o host-raw-gadget.o device-libusb.o proxy.o misc.o input-device.o $(LDFLAG) -o $(PROGRAM)

%.o: %.cpp %.h
	g++ $(CFLAGS) -c $<

%.o: %.cpp
	g++ $(CFLAGS) -c $<

clean:
	-rm *.o
	-rm $(PROGRAM)

setup:
	sudo apt install libusb-1.0-0-dev	
	git clone https://github.com/xairy/raw-gadget.git
	ptach < raw_gadget.patch
	cd raw-gadget/raw_gadget && make
	sudo mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/usb/raw_gadget
	sudo cp raw-gadget/raw_gadget/raw_gadget.ko /lib/modules/$(shell uname -r)/kernel/drivers/usb/raw_gadget/raw_gadget.ko
	sudo depmod
	echo "dtoverlay=dwc2" | sudo tee -a /boot/config.txt
	echo "dwc2" | sudo tee -a /etc/modules
	echo "raw_gadget" | sudo tee -a /etc/modules

install: $(PROGRAM)
	sudo cp -f raspi-g29-mixer.service /lib/systemd/system/.
	sudo cp -f raspi-g29-mixer /usr/local/bin/.
	sudo chmod 644 /lib/systemd/system/raspi-g29-mixer.service
	sudo systemctl daemon-reload
	sudo systemctl enable raspi-g29-mixer.service

start:
	sudo systemctl start raspi-g29-mixer.service

stop:
	sudo systemctl stop raspi-g29-mixer.service

status:
	sudo systemctl status raspi-g29-mixer.service
