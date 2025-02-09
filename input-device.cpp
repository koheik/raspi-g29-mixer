#include "input-device.h"
#include <vector>

libusb_context* InputDevice::context = NULL;

int InputDevice::hotplug_callback(struct libusb_context *ctx __attribute__((unused)),
			struct libusb_device *dev __attribute__((unused)),
			libusb_hotplug_event envet __attribute__((unused)),
			void *user_data __attribute__((unused)))
{
	printf("Hotplug event\n");

	kill(0, SIGINT);
	return 0;
}

void *InputDevice::hotplug_monitor(void *arg __attribute__((unused)))
{
	printf("Start hotplug_monitor thread, thread id(%d)\n", gettid());
	while(true) {
		usleep(100 * 1000);
		libusb_handle_events_completed(NULL, NULL);
	}
}

int InputDevice::get_descriptor(libusb_device *device)
{
	this->device = device;

	int result;
	result = libusb_get_device_descriptor(device, &device_device_desc);
	if (result != LIBUSB_SUCCESS) {
		if (verbose_level) {
			fprintf(stderr, "Error retrieving device descriptor: %s\n",
					libusb_strerror((libusb_error)result));
		}
		return result;
	}
	if (verbose_level) {
		printf("vendor=%x\n", device_device_desc.idVendor);
		printf("product=%x\n", device_device_desc.idProduct);
		printf("serial=%x\n", device_device_desc.iSerialNumber);
	}

	device_config_desc = new struct libusb_config_descriptor *[device_device_desc.bNumConfigurations];
	for (int i = 0; i < device_device_desc.bNumConfigurations; i++) {
		result = libusb_get_config_descriptor(device, i, &device_config_desc[i]);
		if (result != LIBUSB_SUCCESS) {
			if (verbose_level) {
				fprintf(stderr, "Error retrieving configuration(%d) descriptor: %s\n",
						i, libusb_strerror((libusb_error)result));
			}
			return result;
		}

		if (verbose_level) {
			libusb_config_descriptor *config_desc = device_config_desc[i];
			for (int j = 0; j < config_desc->bNumInterfaces; j++) {
				const libusb_interface interface = config_desc->interface[j];
				for (int k = 0; k < interface.num_altsetting; k++) {
					const libusb_interface_descriptor altsetting = interface.altsetting[k];
					for (int l = 0; l < altsetting.bNumEndpoints; l++) {
						const libusb_endpoint_descriptor endpoint = altsetting.endpoint[k];
						printf("conf=%d int=%d alt=%d addr=%x\n", j, k, l, endpoint.bEndpointAddress);
					}

				}

			}
		}
	}

	return LIBUSB_SUCCESS;
}

std::vector<InputDevice *>* InputDevice::connect(int vendor_id, int product_id)
{
	int result;
	std::vector<InputDevice *> *found = new std::vector<InputDevice *>();

	result = libusb_init(&context);
	if (result < 0) {
		fprintf(stderr, "Init error: %s\n", libusb_strerror((libusb_error)result));
		delete found;
		return NULL;
	}
	libusb_set_debug(context, 3);

	libusb_device **list = NULL;
	// libusb_device *found = NULL;

	int cnt = libusb_get_device_list(context, &list);
	if (cnt < 0) {
		if (verbose_level) {
			fprintf(stderr, "Error retrieving device list: %s\n",
					libusb_strerror((libusb_error)cnt));
		}
		delete found;
		return NULL;
	}

	if (verbose_level)
		printf("%d Devices in list\n", cnt);

	for (int i = 0; i < cnt; i++) {
		libusb_device *dvc = list[i];
		InputDevice *id = new InputDevice();
		result = id->get_descriptor(dvc);
		if (result != LIBUSB_SUCCESS) {
			delete id;
			continue;
		}

		if (id->device_device_desc.bDeviceClass == LIBUSB_CLASS_HUB) {
			delete id;
			continue;
		}

		if ((vendor_id == id->device_device_desc.idVendor || vendor_id == LIBUSB_HOTPLUG_MATCH_ANY) &&
			(product_id == id->device_device_desc.idProduct || product_id == LIBUSB_HOTPLUG_MATCH_ANY)) {
			found->push_back(id);
		}
	}

	// libusb_free_device_list(list, 1);


	if (found->size() == 0) {
		printf("Target device not found\n");
		delete found;
		return NULL;
	}

	return found;
}

int InputDevice::connect_device() {
	int result;

	result = libusb_open(device, &dev_handle);
	if (result != LIBUSB_SUCCESS) {
		if (verbose_level) {
			fprintf(stderr, "Error opening device handle: %s\n",
					libusb_strerror((libusb_error)result));
		}
		dev_handle = NULL;
		// libusb_free_device_list(list, 1);
		return result;
	}

	result = libusb_set_auto_detach_kernel_driver(dev_handle, 0);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "libusb_set_auto_detach_kernel_driver() failed: %s\n",
				libusb_strerror((libusb_error)result));
		return result;
	}

	int config = 0;
	result = libusb_get_configuration(dev_handle, &config);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "libusb_get_configuration() failed: %s\n",
				libusb_strerror((libusb_error)result));
		return result;
	}

	for (int i = 0; i < device_device_desc.bNumConfigurations; i++) {
		if (device_config_desc[i]->bConfigurationValue != config)
			continue;
		for (int j = 0; j < device_config_desc[i]->bNumInterfaces; j++)
			libusb_detach_kernel_driver(dev_handle, j);
	}

	if (reset_device_before_proxy) {
		result = libusb_reset_device(dev_handle);
		if (result != LIBUSB_SUCCESS) {
			fprintf(stderr, "libusb_reset_device() failed: %s\n",
					libusb_strerror((libusb_error)result));
			return result;
		}
		usleep(1000);
	}

	//check that device is responsive
	unsigned char unused[4];
	result = libusb_get_string_descriptor(dev_handle, 0, 0, unused, sizeof(unused));
	if (result < 0) {
		fprintf(stderr, "Device unresponsive: %s\n",
				libusb_strerror((libusb_error)result));
		return result;
	}

	if (callback_handle == -1) {
		result = libusb_hotplug_register_callback(context,
			(libusb_hotplug_event) (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
			(libusb_hotplug_flag) 0, device_device_desc.idVendor , device_device_desc.idProduct,
			LIBUSB_HOTPLUG_MATCH_ANY, InputDevice::hotplug_callback, NULL, &callback_handle);

		if (result != LIBUSB_SUCCESS) {
			fprintf(stderr, "Error registering callback\n");
			libusb_exit(context);
			return result;
		}
		pthread_create(&hotplug_monitor_thread, 0,
			InputDevice::hotplug_monitor, nullptr);
	}

	return 0;
}

void InputDevice::reset_device() {
	int result = libusb_reset_device(dev_handle);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error resetting device: %s\n",
				libusb_strerror((libusb_error)result));
	}
}

void InputDevice::set_configuration(int configuration) {
	int result = libusb_set_configuration(dev_handle, configuration);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error setting configuration(%d): %s\n",
				configuration, libusb_strerror((libusb_error)result));
	}
}

void InputDevice::claim_interface(int interface) {
	int result = libusb_claim_interface(dev_handle, interface);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error claiming interface(%d): %s\n",
				interface, libusb_strerror((libusb_error)result));
	}
}

void InputDevice::release_interface(int interface) {
	int result = libusb_release_interface(dev_handle, interface);
	if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_FOUND) {
		fprintf(stderr, "Error releasing interface(%d): %s\n",
				interface, libusb_strerror((libusb_error)result));
	}
}

void InputDevice::set_interface_alt_setting(int interface, int altsetting) {
	int result = libusb_set_interface_alt_setting(dev_handle, interface, altsetting);
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Error setting interface altsetting(%d, %d): %s\n",
				interface, altsetting, libusb_strerror((libusb_error)result));
	}
}

int InputDevice::control_request(const usb_ctrlrequest *setup_packet, int *nbytes,
			unsigned char **dataptr, int timeout) {
	int result = libusb_control_transfer(dev_handle,
					setup_packet->bRequestType, setup_packet->bRequest,
					setup_packet->wValue, setup_packet->wIndex, *dataptr,
					setup_packet->wLength, timeout);

	if (result < 0) {
		if (verbose_level) {
			fprintf(stderr, "Error sending setup packet: %s\n",
					libusb_strerror((libusb_error)result));
		}
		if (result == LIBUSB_ERROR_PIPE)
			return -1;
		return result;
	}
	else {
		if (verbose_level)
			printf("Control transfer succeed\n");
	}

	*nbytes = result;
	return 0;
}

int InputDevice::send_data(uint8_t endpoint, uint8_t attributes, uint8_t *dataptr,
			int length) {
	int transferred;
	int attempt = 0;
	int result = LIBUSB_SUCCESS;

	bool incomplete_transfer = false;

	switch (attributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		fprintf(stderr, "Can't send on a control endpoint.\n");
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (verbose_level)
			fprintf(stderr, "Isochronous(write) endpoint EP%02x unhandled.\n", endpoint);
		break;
	case USB_ENDPOINT_XFER_BULK:
		do {
			result = libusb_bulk_transfer(dev_handle, endpoint, dataptr, length, &transferred, 0);
			//TODO retry transfer if incomplete
			if (transferred != length) {
				fprintf(stderr, "Incomplete Bulk transfer on EP%02x for attempt %d. length(%d), transferred(%d)\n",
					endpoint, attempt, length, transferred);
				incomplete_transfer = true;
			}
			if (result == LIBUSB_SUCCESS) {
				if (incomplete_transfer)
					printf("Resent Bulk transfer on EP%02x for attempt %d. length(%d), transferred(%d)\n",
						endpoint, attempt, length, transferred);
				if (verbose_level > 2)
					printf("Sent %d bytes (Bulk) to EP%02x\n", transferred, endpoint);
			}
			if ((result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_TIMEOUT))
				libusb_clear_halt(dev_handle, endpoint);

			attempt++;
		} while ((result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_TIMEOUT || transferred != length)
					&& attempt < ID_MAX_ATTEMPTS);
		break;
	case USB_ENDPOINT_XFER_INT:
		result = libusb_interrupt_transfer(dev_handle, endpoint, dataptr, length, &transferred, 0);

		if (transferred != length)
			fprintf(stderr, "Incomplete Interrupt transfer on EP%02x\n", endpoint);
		if (result == LIBUSB_SUCCESS && verbose_level > 2)
			printf("Sent %d bytes (Int) to libusb EP%02x\n", transferred, endpoint);
		break;
	}
	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Transfer error sending on EP%02x: %s\n",
				endpoint, libusb_strerror((libusb_error)result));
	}
	return result;
}

int InputDevice::receive_data(uint8_t endpoint, uint8_t attributes, uint16_t maxPacketSize,
			uint8_t **dataptr, int *length, int timeout) {
	int result = LIBUSB_SUCCESS;
	timeout = 0;

	int attempt = 0;
	switch (attributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		fprintf(stderr, "Can't read on a control endpoint.\n");
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (verbose_level)
			fprintf(stderr, "Isochronous(read) endpoint EP%02x unhandled.\n", endpoint);
		break;
	case USB_ENDPOINT_XFER_BULK:
		*dataptr = new uint8_t[maxPacketSize * 8];
		do {
			result = libusb_bulk_transfer(dev_handle, endpoint, *dataptr, maxPacketSize, length, timeout);
			if (result == LIBUSB_SUCCESS && verbose_level > 2)
				printf("Received bulk data(%d) bytes\n", *length);
			if ((result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_TIMEOUT))
				libusb_clear_halt(dev_handle, endpoint);

			attempt++;
		} while ((result == LIBUSB_ERROR_PIPE || result == LIBUSB_ERROR_TIMEOUT) && attempt < ID_MAX_ATTEMPTS);
		break;
	case USB_ENDPOINT_XFER_INT:
		*dataptr = new uint8_t[maxPacketSize];
		result = libusb_interrupt_transfer(dev_handle, endpoint, *dataptr, maxPacketSize, length, timeout);
		if (result == LIBUSB_SUCCESS && verbose_level > 2)
			printf("Received int data(%d) bytes\n", *length);
		break;
	}

	if (result != LIBUSB_SUCCESS) {
		fprintf(stderr, "Transfer error receiving on EP%02x: %s\n",
				endpoint, libusb_strerror((libusb_error)result));
	}

	return result;
}

InputDevice::~InputDevice()
{
	delete[] device_config_desc;

	if (context && callback_handle != -1) {
		libusb_hotplug_deregister_callback(context, callback_handle);
	}
	if (hotplug_monitor_thread &&
		pthread_join(hotplug_monitor_thread, NULL)) {
		fprintf(stderr, "Error join hotplug_monitor_thread\n");
	}
}
