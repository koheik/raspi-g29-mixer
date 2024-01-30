#include <vector>

#include "host-raw-gadget.h"
#include "device-libusb.h"
#include "misc.h"

#include "input-device.h"


void printData(struct usb_raw_transfer_io io, __u8 bEndpointAddress, std::string transfer_type, std::string dir) {
	printf("Sending data to EP%x(%s_%s):", bEndpointAddress,
		transfer_type.c_str(), dir.c_str());
	for (unsigned int i = 0; i < io.inner.length; i++) {
		printf(" %02hhx", (unsigned)io.data[i]);
	}
	printf("\n");
}

void mix(unsigned char *w, unsigned char *t)
{
	w[1] = (w[1] & 0xfc) | ((t[2] & 0x04) >> 1) | ((t[2] & 0x08) >> 3);
	w[2] = (w[2] & 0x7f) | ((t[2] & 0x01) << 7);
	w[3] = (w[3] & 0xfe) | ((t[2] & 0x02) >> 1);	
}

void *ep_loop_write(void *arg) {
	struct thread_info thread_info = *((struct thread_info*) arg);
	int fd = thread_info.fd;
	int ep_num = thread_info.ep_num;
	struct usb_endpoint_descriptor ep = thread_info.endpoint;
	std::string transfer_type = thread_info.transfer_type;
	std::string dir = thread_info.dir;
	std::deque<usb_raw_transfer_io> *data_queue = thread_info.data_queue;
	std::mutex *data_mutex = thread_info.data_mutex;

	static unsigned char wheel_data[12];
	static unsigned char trim_data[6];
	static bool wheel_init = false;
	static bool trim_init = false;

	printf("Start writing thread for EP%02x, thread id(%d)\n",
		ep.bEndpointAddress, gettid());

	while (!please_stop_eps) {
		assert(ep_num != -1);
		if (data_queue->size() == 0) {
			usleep(100);
			continue;
		}

		data_mutex->lock();
		struct usb_raw_transfer_io io = data_queue->front();
		data_queue->pop_front();
		data_mutex->unlock();

		if (verbose_level >= 2)
			printData(io, ep.bEndpointAddress, transfer_type, dir);

		int length = io.inner.length;
		if (ep.bEndpointAddress == 0x81
			&& io.inner.ep == 0x84
			&& io.inner.length == 3
			&& io.data[0] == 0x03)
		{
			memcpy(&trim_data, io.data, length);
			trim_init = true;

			if (verbose_level) {
				for (int i = 0; i < length; i++) {
					printf(" %02x", trim_data[i]);
				}
				printf("\n");
			}

			if (wheel_init) {
				mix(wheel_data, trim_data);
				io.inner.length = 12;
				io.inner.ep = 0x00;
				memcpy(io.data, wheel_data, 12);
				int rv = usb_raw_ep_write(fd, (struct usb_raw_ep_io *)&io);
				if (rv < 0 && errno == ESHUTDOWN) {
					printf("EP%x(%s_%s): device likely reset, stopping thread\n",
						ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
					break;
				} else if (rv < 0) {
					perror("usb_raw_ep_write()");
					exit(EXIT_FAILURE);
				} else if (verbose_level) {
					printf("EP%x(%s_%s): wrote %d bytes to host\n", ep.bEndpointAddress,
						transfer_type.c_str(), dir.c_str(), rv);
					printData(io, ep.bEndpointAddress, transfer_type, dir);
				}
			}
		} else if (ep.bEndpointAddress == 0x81
			&& io.inner.ep == 0x84)
			// ignore here
		{
		} else if (ep.bEndpointAddress == 0x81 
			&& io.inner.length == 12
			&& io.data[0] == 0x08)
		{
			memcpy(&wheel_data, io.data, length);
			wheel_init = true;

			if (trim_init) {
				mix(wheel_data, trim_data);
				memcpy(io.data, &wheel_data, length);
			}
			int rv = usb_raw_ep_write(fd, (struct usb_raw_ep_io *)&io);
			if (rv < 0 && errno == ESHUTDOWN) {
				printf("EP%x(%s_%s): device likely reset, stopping thread\n",
					ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
				break;
			}
			else if (rv < 0) {
				perror("usb_raw_ep_write()");
				exit(EXIT_FAILURE);
			}
			else {
				if (verbose_level) {
					printf("EP%x(%s_%s): wrote %d bytes to host\n", ep.bEndpointAddress,
						transfer_type.c_str(), dir.c_str(), rv);
					printData(io, ep.bEndpointAddress, transfer_type, dir);
				}
			}
		} else if (ep.bEndpointAddress & USB_DIR_IN) {
			int rv = usb_raw_ep_write(fd, (struct usb_raw_ep_io *)&io);
			if (rv < 0 && errno == ESHUTDOWN) {
				printf("EP%x(%s_%s): device likely reset, stopping thread\n",
					ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
				break;
			}
			else if (rv < 0) {
				perror("usb_raw_ep_write()");
				exit(EXIT_FAILURE);
			}
			else {
				if (verbose_level) {
					printf("EP%x(%s_%s): wrote %d bytes to host\n", ep.bEndpointAddress,
						transfer_type.c_str(), dir.c_str(), rv);
					printData(io, ep.bEndpointAddress, transfer_type, dir);
				}
			}
		}
		else {
			int length = io.inner.length;
			unsigned char *data = new unsigned char[length];
			memcpy(data, io.data, length);
			int rv = send_data(ep.bEndpointAddress, ep.bmAttributes, data, length);
			if (rv == LIBUSB_ERROR_NO_DEVICE) {
				printf("EP%x(%s_%s): device likely reset, stopping thread\n",
					ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
				break;
			}

			if (data)
				delete[] data;
		}
	}

	printf("End writing thread for EP%02x, thread id(%d)\n",
		ep.bEndpointAddress, gettid());
	return NULL;
}

void *trim_loop_read(void *arg)
{
	struct thread_info thread_info = *((struct thread_info*) arg);
	InputDevice *trim = thread_info.trim;
	// int ep_num = thread_info.ep_num;
	// struct usb_endpoint_descriptor ep = thread_info.endpoint;
	// std::string transfer_type = thread_info.transfer_type;
	// std::string dir = thread_info.dir;
	std::deque<usb_raw_transfer_io> *data_queue = thread_info.data_queue;
	std::mutex *data_mutex = thread_info.data_mutex;

	if (verbose_level) {
		printf("Start reading thread fort trim device, thread id(%d)\n", gettid());
	}
		
	while (!please_stop_eps) {
		struct usb_raw_transfer_io io;

		if (data_queue->size() >= 32) {
			usleep(200);
			continue;
		}

		unsigned char *data = NULL;
		int nbytes = -1;
		if (verbose_level > 2) {
			printf("waiting data from trim device, thread id(%d)\n", gettid());
		}
		int rv = trim->receive_data(0x84, USB_ENDPOINT_XFER_INT, 64, &data, &nbytes, 0);
		if (verbose_level > 2) {
			printf("received data from trim device, thread id(%d)\n", gettid());
		}
		if (rv == LIBUSB_ERROR_NO_DEVICE) {
			printf("EP%x(%s_%s): device likely reset, stopping thread\n", 0x84, "int", "in");
			break;
		}

		if (nbytes >= 0) {
			memcpy(io.data, data, nbytes);
			io.inner.ep = 0x84;
			io.inner.flags = 0;
			io.inner.length = nbytes;

			data_mutex->lock();
			data_queue->push_back(io);
			data_mutex->unlock();
			if (verbose_level)
				printf("EP%x(%s_%s): enqueued %d bytes to queue\n", 0x84, "int", "in", nbytes);				
		}

		if (data)
			delete[] data;
	}
	printf("End reading thread for EP84, thread id(%d)\n", gettid());
	return NULL;
}

void *ep_loop_read(void *arg) {
	struct thread_info thread_info = *((struct thread_info*) arg);
	int fd = thread_info.fd;
	int ep_num = thread_info.ep_num;
	struct usb_endpoint_descriptor ep = thread_info.endpoint;
	std::string transfer_type = thread_info.transfer_type;
	std::string dir = thread_info.dir;
	std::deque<usb_raw_transfer_io> *data_queue = thread_info.data_queue;
	std::mutex *data_mutex = thread_info.data_mutex;

	if (verbose_level) {
		printf("Start reading thread for EP%02x, thread id(%d)\n",
			ep.bEndpointAddress, gettid());
	}

	while (!please_stop_eps) {
		assert(ep_num != -1);
		struct usb_raw_transfer_io io;

		if (ep.bEndpointAddress & USB_DIR_IN) {
			unsigned char *data = NULL;
			int nbytes = -1;

			if (data_queue->size() >= 32) {
				usleep(200);
				continue;
			}

			int rv = receive_data(ep.bEndpointAddress, ep.bmAttributes, ep.wMaxPacketSize, &data, &nbytes, 0);
			if (rv == LIBUSB_ERROR_NO_DEVICE) {
				printf("EP%x(%s_%s): device likely reset, stopping thread\n",
					ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
				break;
			}

			if (nbytes >= 0) {
				memcpy(io.data, data, nbytes);
				io.inner.ep = ep_num;
				io.inner.flags = 0;
				io.inner.length = nbytes;

				data_mutex->lock();
				data_queue->push_back(io);
				data_mutex->unlock();
				if (verbose_level)
					printf("EP%x(%s_%s): enqueued %d bytes to queue\n", ep.bEndpointAddress,
							transfer_type.c_str(), dir.c_str(), nbytes);
			}

			if (data)
				delete[] data;
		}
		else {
			io.inner.ep = ep_num;
			io.inner.flags = 0;
			io.inner.length = sizeof(io.data);

			int rv = usb_raw_ep_read(fd, (struct usb_raw_ep_io *)&io);
			if (rv < 0 && errno == ESHUTDOWN) {
				printf("EP%x(%s_%s): device likely reset, stopping thread\n",
					ep.bEndpointAddress, transfer_type.c_str(), dir.c_str());
				break;
			}
			else if (rv < 0) {
				perror("usb_raw_ep_read()");
				exit(EXIT_FAILURE);
			}
			else {
				if (verbose_level) {
					printf("EP%x(%s_%s): read %d bytes from host\n", ep.bEndpointAddress,
							transfer_type.c_str(), dir.c_str(), rv);
				}
				io.inner.length = rv;

				data_mutex->lock();
				data_queue->push_back(io);
				data_mutex->unlock();
				if (verbose_level)
					printf("EP%x(%s_%s): enqueued %d bytes to queue\n", ep.bEndpointAddress,
							transfer_type.c_str(), dir.c_str(), rv);
			}
		}
	}

	if (verbose_level) {
		printf("End reading thread for EP%02x, thread id(%d)\n",
			ep.bEndpointAddress, gettid());
	}
	return NULL;
}

void process_eps(int fd, int config, int interface, int altsetting, std::vector<InputDevice*> *trims)
{
	struct raw_gadget_altsetting *alt = &host_device_desc.configs[config]
					.interfaces[interface].altsettings[altsetting];

	if (verbose_level) {
		printf("Activating %d endpoints on interface %d\n", (int)alt->interface.bNumEndpoints, interface);
	}

	for (int i = 0; i < alt->interface.bNumEndpoints; i++) {
		struct raw_gadget_endpoint *ep = &alt->endpoints[i];

		int addr = usb_endpoint_num(&ep->endpoint);
		assert(addr != 0);

		ep->thread_info.fd = fd;
		ep->thread_info.endpoint = ep->endpoint;
		ep->thread_info.data_queue = new std::deque<usb_raw_transfer_io>;
		ep->thread_info.data_mutex = new std::mutex;

		switch (usb_endpoint_type(&ep->endpoint)) {
		case USB_ENDPOINT_XFER_ISOC:
			ep->thread_info.transfer_type = "isoc";
			break;
		case USB_ENDPOINT_XFER_BULK:
			ep->thread_info.transfer_type = "bulk";
			break;
		case USB_ENDPOINT_XFER_INT:
			ep->thread_info.transfer_type = "int";
			break;
		default:
			printf("transfer_type %d is invalid\n", usb_endpoint_type(&ep->endpoint));
			assert(false);
		}

		if (usb_endpoint_dir_in(&ep->endpoint))
			ep->thread_info.dir = "in";
		else
			ep->thread_info.dir = "out";

		ep->thread_info.ep_num = usb_raw_ep_enable(fd, &ep->thread_info.endpoint);
		printf("%s_%s: addr = %u, ep = #%d\n",
			ep->thread_info.transfer_type.c_str(),
			ep->thread_info.dir.c_str(),
			addr, ep->thread_info.ep_num);

		if (verbose_level)
			printf("Creating thread for EP%02x\n",
				ep->thread_info.endpoint.bEndpointAddress);
		pthread_create(&ep->thread_read, 0,
			ep_loop_read, (void *)&ep->thread_info);
		pthread_create(&ep->thread_write, 0,
			ep_loop_write, (void *)&ep->thread_info);

		if (ep->thread_info.endpoint.bEndpointAddress == 0x81)
		{
			size_t n = trims->size();
			ep->n_trim_thread_read = n;
			for (size_t i = 0; i < n; i++) {
				InputDevice *trim = trims->at(i);
				struct thread_info *ti = new struct thread_info;
				memcpy(ti, &ep->thread_info, sizeof(thread_info));
				ti->trim = trim;
				pthread_create(&ep->trim_thread_read[i], 0, trim_loop_read, (void*)ti);
			}
		}
	}

	if (verbose_level) {
		printf("process_eps done\n");
	}
}

void terminate_eps(int fd, int config, int interface, int altsetting)
{
	struct raw_gadget_altsetting *alt = &host_device_desc.configs[config]
					.interfaces[interface].altsettings[altsetting];

	please_stop_eps = true;

	for (int i = 0; i < alt->interface.bNumEndpoints; i++) {
		struct raw_gadget_endpoint *ep = &alt->endpoints[i];

		if (ep->thread_read && pthread_join(ep->thread_read, NULL)) {
			fprintf(stderr, "Error join thread_read\n");
		}
		if (ep->thread_write && pthread_join(ep->thread_write, NULL)) {
			fprintf(stderr, "Error join thread_write\n");
		}
		for (size_t i = 0; i < ep->n_trim_thread_read; i++) {
			if (ep->trim_thread_read[i] && pthread_join(ep->trim_thread_read[i], NULL)) {
				fprintf(stderr, "Error join trim_thread_read\n");
			}
		}
		ep->thread_read = 0;
		ep->thread_write = 0;
		for (size_t i = 0; i < ep->n_trim_thread_read; i++)
			ep->trim_thread_read[i] = 0;
		ep->n_trim_thread_read = 0;

		usb_raw_ep_disable(fd, ep->thread_info.ep_num);
		ep->thread_info.ep_num = -1;

		delete ep->thread_info.data_queue;
		delete ep->thread_info.data_mutex;
	}

	please_stop_eps = false;
}

void ep0_loop(int fd, std::vector<InputDevice *> *trims) {
	bool set_configuration_done_once = false;

	if (verbose_level) {
		printf("Start for EP0, thread id(%d)\n", gettid());
	}

	if (verbose_level)
		print_eps_info(fd);

	while (!please_stop_ep0) {
		struct usb_raw_control_event event;
		event.inner.type = 0;
		event.inner.length = sizeof(event.ctrl);

		usb_raw_event_fetch(fd, (struct usb_raw_event *)&event);
		if (verbose_level)
			log_event((struct usb_raw_event *)&event);

		if (event.inner.length == 4294967295) {
			printf("End for EP0, thread id(%d)\n", gettid());
			return;
		}

		// Normally, we would only need to check for USB_RAW_EVENT_RESET to handle a reset event.
		// However, dwc2 is buggy and it reports a disconnect event instead of a reset.
		if (event.inner.type == USB_RAW_EVENT_RESET || event.inner.type == USB_RAW_EVENT_DISCONNECT) {
			printf("Resetting device\n");
			// Normally, we would need to stop endpoint threads first and only then
			// reset the device. However, libusb does not allow interrupting queued
			// requests submitted via sync I/O. Thus, we reset the proxied device to
			// force libusb to interrupt the requests and allow the endpoint threads
			// to exit on please_stop_eps checks.
			if (set_configuration_done_once)
				please_stop_eps = true;
			reset_device();
			if (set_configuration_done_once) {
				struct raw_gadget_config *config = &host_device_desc.configs[host_device_desc.current_config];
				printf("Stopping endpoint threads\n");
				for (int i = 0; i < config->config.bNumInterfaces; i++) {
					struct raw_gadget_interface *iface = &config->interfaces[i];
					int interface_num = iface->altsettings[iface->current_altsetting]
						.interface.bInterfaceNumber;
					terminate_eps(fd, host_device_desc.current_config, i,
							iface->current_altsetting);
					release_interface(interface_num);
					iface->current_altsetting = 0;
				}
				printf("Endpoint threads stopped\n");
				host_device_desc.current_config = 0;
				set_configuration_done_once = false;
			}
			continue;
		}

		if (event.inner.type != USB_RAW_EVENT_CONTROL)
			continue;

		struct usb_raw_transfer_io io;
		io.inner.ep = 0;
		io.inner.flags = 0;
		io.inner.length = event.ctrl.wLength;

		int nbytes = 0;
		int result = 0;
		unsigned char *control_data = new unsigned char[event.ctrl.wLength];

		int rv = -1;
		if (event.ctrl.bRequestType & USB_DIR_IN) {
			result = control_request(&event.ctrl, &nbytes, &control_data, 1000);
			if (result == 0) {
				memcpy(&io.data[0], control_data, nbytes);
				io.inner.length = nbytes;

				// Some UDCs require bMaxPacketSize0 to be at least 64.
				// Ideally, the information about UDC limitations needs to be
				// exposed by Raw Gadget, but this is not implemented at the moment;
				// see https://github.com/xairy/raw-gadget/issues/41.
				if (bmaxpacketsize0_must_greater_than_64 &&
				    (event.ctrl.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
				    event.ctrl.bRequest == USB_REQ_GET_DESCRIPTOR &&
				    (event.ctrl.wValue >> 8) == USB_DT_DEVICE) {
					struct usb_device_descriptor *dev = (struct usb_device_descriptor *)&io.data;
					if (dev->bMaxPacketSize0 < 64)
						dev->bMaxPacketSize0 = 64;
				}

				if (verbose_level >= 2)
					printData(io, 0x00, "control", "in");

				rv = usb_raw_ep0_write(fd, (struct usb_raw_ep_io *)&io);
				if (verbose_level)
					printf("ep0: transferred %d bytes (in)\n", rv);
			}
			else {
				usb_raw_ep0_stall(fd);
			}
		}
		else {
			if ((event.ctrl.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
					event.ctrl.bRequest == USB_REQ_SET_CONFIGURATION) {
				int desired_config = -1;
				for (int i = 0; i < host_device_desc.device.bNumConfigurations; i++) {
					if (host_device_desc.configs[i].config.bConfigurationValue == event.ctrl.wValue) {
						desired_config = i;
						break;
					}
				}
				if (desired_config < 0) {
					printf("[Warning] Skip changing configuration, wValue(%d) is invalid\n", event.ctrl.wValue);
					continue;
				}

				struct raw_gadget_config *config = &host_device_desc.configs[desired_config];

				if (set_configuration_done_once) { // Need to stop all threads for eps and cleanup
					printf("Changing configuration\n");
					for (int i = 0; i < config->config.bNumInterfaces; i++) {
						struct raw_gadget_interface *iface = &config->interfaces[i];
						int interface_num = iface->altsettings[iface->current_altsetting]
							.interface.bInterfaceNumber;
						terminate_eps(fd, host_device_desc.current_config, i,
								iface->current_altsetting);
						release_interface(interface_num);
					}
				}

				usb_raw_configure(fd);
				set_configuration(config->config.bConfigurationValue);
				host_device_desc.current_config = desired_config;

				for (int i = 0; i < config->config.bNumInterfaces; i++) {
					struct raw_gadget_interface *iface = &config->interfaces[i];
					iface->current_altsetting = 0;
					int interface_num = iface->altsettings[0].interface.bInterfaceNumber;
					claim_interface(interface_num);
					process_eps(fd, desired_config, i, 0, trims);
					usleep(10000); // Give threads time to spawn.
				}

				set_configuration_done_once = true;

				// Ack request after spawning endpoint threads.
				rv = usb_raw_ep0_read(fd, (struct usb_raw_ep_io *)&io);
			}
			else if ((event.ctrl.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
					event.ctrl.bRequest == USB_REQ_SET_INTERFACE) {
				struct raw_gadget_config *config =
					&host_device_desc.configs[host_device_desc.current_config];

				int desired_interface = -1;
				for (int i = 0; i < config->config.bNumInterfaces; i++) {
					if (config->interfaces[i].altsettings[0].interface.bInterfaceNumber ==
							event.ctrl.wIndex) {
						desired_interface = i;
						break;
					}
				}
				if (desired_interface < 0) {
					printf("[Warning] Skip changing interface, wIndex(%d) is invalid\n", event.ctrl.wIndex);
					continue;
				}

				struct raw_gadget_interface *iface = &config->interfaces[desired_interface];

				int desired_altsetting = -1;
				for (int i = 0; i < iface->num_altsettings; i++) {
					if (iface->altsettings[i].interface.bAlternateSetting == event.ctrl.wValue) {
						desired_altsetting = i;
						break;
					}
				}
				if (desired_altsetting < 0) {
					printf("[Warning] Skip changing alt_setting, wValue(%d) is invalid\n", event.ctrl.wValue);
					continue;
				}

				struct raw_gadget_altsetting *alt = &iface->altsettings[desired_altsetting];

				if (desired_altsetting == iface->current_altsetting) {
					printf("Interface/altsetting already set\n");
					// But lets propagate the request to the device.
					set_interface_alt_setting(alt->interface.bInterfaceNumber,
						alt->interface.bAlternateSetting);
				}
				else {
					printf("Changing interface/altsetting\n");
					terminate_eps(fd, host_device_desc.current_config,
						desired_interface, iface->current_altsetting);
					set_interface_alt_setting(alt->interface.bInterfaceNumber,
						alt->interface.bAlternateSetting);
					process_eps(fd, host_device_desc.current_config,
						desired_interface, desired_altsetting, trims);
					iface->current_altsetting = desired_altsetting;
					usleep(10000); // Give threads time to spawn.
				}

				// Ack request after spawning endpoint threads.
				rv = usb_raw_ep0_read(fd, (struct usb_raw_ep_io *)&io);
			}
			else {
				// Retrieve data for sending request to proxied device.
				rv = usb_raw_ep0_read(fd, (struct usb_raw_ep_io *)&io);

				memcpy(control_data, io.data, event.ctrl.wLength);

				if (verbose_level >= 2)
					printData(io, 0x00, "control", "out");

				if (event.ctrl.bRequestType == 0x21 && event.ctrl.bRequest == 0x0a) {
					// This SET_IDLE requests somehow fail so just ignore here
					continue;
				}
				result = control_request(&event.ctrl, &nbytes, &control_data, 1000);
				if (result == 0) {
					if (verbose_level)
						printf("ep0: transferred %d bytes (out)\n", rv);
				}
				else {
					usb_raw_ep0_stall(fd);
				}
			}
		}

		delete[] control_data;
	}

	struct raw_gadget_config *config = &host_device_desc.configs[host_device_desc.current_config];

	for (int i = 0; i < config->config.bNumInterfaces; i++) {
		struct raw_gadget_interface *iface = &config->interfaces[i];
		int interface_num = iface->altsettings[iface->current_altsetting]
			.interface.bInterfaceNumber;
		terminate_eps(fd, host_device_desc.current_config, i,
				iface->current_altsetting);
		release_interface(interface_num);
	}

	printf("End for EP0, thread id(%d)\n", gettid());
}
