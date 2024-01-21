#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H
#include <libusb-1.0/libusb.h>

#include "misc.h"

class InputDevice
{
    libusb_device			            **devs;
    libusb_device_handle	        	*dev_handle;
    libusb_context			            *context;
    libusb_hotplug_callback_handle	    callback_handle;

    struct libusb_device_descriptor		device_device_desc;
    struct libusb_config_descriptor		**device_config_desc;

    pthread_t hotplug_monitor_thread;

    const static int ID_MAX_ATTEMPTS = 5;

    static int hotplug_callback(struct libusb_context *ctx __attribute__((unused)),
			struct libusb_device *dev __attribute__((unused)),
			libusb_hotplug_event envet __attribute__((unused)),
			void *user_data __attribute__((unused)));

    static void *hotplug_monitor(void *arg __attribute__((unused)));
    int get_descriptor(libusb_device *device);

public:
    ~InputDevice();
    int connect_device(int vendorId, int productId);
    void reset_device();
    void set_configuration(int configuration);
    void claim_interface(int interface);
    void release_interface(int interface);
    void set_interface_alt_setting(int interface, int altsetting);
    int control_request(const usb_ctrlrequest *setup_packet, int *nbytes,
                unsigned char **dataptr, int timeout);
    int send_data(uint8_t endpoint, uint8_t attributes, uint8_t *dataptr,
                int length);
    int receive_data(uint8_t endpoint, uint8_t attributes, uint16_t maxPacketSize,
                uint8_t **dataptr, int *length, int timeout);
};
#endif