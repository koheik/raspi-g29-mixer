#include <pthread.h>
#include <mutex>
#include <deque>

#include "misc.h"

/*----------------------------------------------------------------------*/

#define UDC_NAME_LENGTH_MAX 128

struct usb_raw_init {
	__u8 driver_name[UDC_NAME_LENGTH_MAX];
	__u8 device_name[UDC_NAME_LENGTH_MAX];
	__u8 speed;
};

enum usb_raw_event_type {
	USB_RAW_EVENT_INVALID = 0,
	USB_RAW_EVENT_CONNECT = 1,
	USB_RAW_EVENT_CONTROL = 2,
	USB_RAW_EVENT_SUSPEND = 3,
	USB_RAW_EVENT_RESUME = 4,
	USB_RAW_EVENT_RESET = 5,
	USB_RAW_EVENT_DISCONNECT = 6,
};

struct usb_raw_event {
	__u32		type;
	__u32		length;
	__u8		data[0];
};

struct usb_raw_ep_io {
	__u16		ep;
	__u16		flags;
	__u32		length;
	__u8		data[0];
};

#define USB_RAW_EPS_NUM_MAX	30
#define USB_RAW_EP_NAME_MAX	16
#define USB_RAW_EP_ADDR_ANY	0xff

struct usb_raw_ep_caps {
	__u32	type_control	: 1;
	__u32	type_iso	: 1;
	__u32	type_bulk	: 1;
	__u32	type_int	: 1;
	__u32	dir_in		: 1;
	__u32	dir_out		: 1;
};

struct usb_raw_ep_limits {
	__u16	maxpacket_limit;
	__u16	max_streams;
	__u32	reserved;
};

struct usb_raw_ep_info {
	__u8				name[USB_RAW_EP_NAME_MAX];
	__u32				addr;
	struct usb_raw_ep_caps		caps;
	struct usb_raw_ep_limits	limits;
};

struct usb_raw_eps_info {
	struct usb_raw_ep_info	eps[USB_RAW_EPS_NUM_MAX];
};

#define USB_RAW_IOCTL_INIT		_IOW('U', 0, struct usb_raw_init)
#define USB_RAW_IOCTL_RUN		_IO('U', 1)
#define USB_RAW_IOCTL_EVENT_FETCH	_IOR('U', 2, struct usb_raw_event)
#define USB_RAW_IOCTL_EP0_WRITE		_IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ		_IOWR('U', 4, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_ENABLE		_IOW('U', 5, struct usb_endpoint_descriptor)
#define USB_RAW_IOCTL_EP_DISABLE	_IOW('U', 6, __u32)
#define USB_RAW_IOCTL_EP_WRITE		_IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_READ		_IOWR('U', 8, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_CONFIGURE		_IO('U', 9)
#define USB_RAW_IOCTL_VBUS_DRAW		_IOW('U', 10, __u32)
#define USB_RAW_IOCTL_EPS_INFO		_IOR('U', 11, struct usb_raw_eps_info)
#define USB_RAW_IOCTL_EP0_STALL		_IO('U', 12)
#define USB_RAW_IOCTL_EP_SET_HALT	_IOW('U', 13, __u32)
#define USB_RAW_IOCTL_EP_CLEAR_HALT	_IOW('U', 14, __u32)
#define USB_RAW_IOCTL_EP_SET_WEDGE	_IOW('U', 15, __u32)

/*----------------------------------------------------------------------*/

#define EP_MAX_PACKET_CONTROL	1024
#define EP_MAX_PACKET_BULK	1024
#define EP_MAX_PACKET_INT	8

struct usb_raw_control_event {
	struct usb_raw_event		inner;
	struct usb_ctrlrequest		ctrl;
};

struct usb_raw_control_io {
	struct usb_raw_ep_io		inner;
	char				data[EP_MAX_PACKET_CONTROL];
};

struct usb_raw_bulk_io {
	struct usb_raw_ep_io		inner;
	char				data[EP_MAX_PACKET_BULK];
};

struct usb_raw_int_io {
	struct usb_raw_ep_io		inner;
	char				data[EP_MAX_PACKET_INT];
};

struct usb_raw_transfer_io {
	struct usb_raw_ep_io		inner;
	char				data[1024];
};

/*----------------------------------------------------------------------*/

struct thread_info {
	int				fd;
	int				ep_num;
	struct usb_endpoint_descriptor 	endpoint;
	std::string			transfer_type;
	std::string			dir;
	std::deque<usb_raw_transfer_io> *data_queue;
	std::mutex			*data_mutex;
};

struct raw_gadget_endpoint {
	struct usb_endpoint_descriptor	endpoint;
	pthread_t			thread_read;
	pthread_t			thread_write;
	struct thread_info		thread_info;
};

struct raw_gadget_altsetting {
	struct usb_interface_descriptor	interface;
	struct raw_gadget_endpoint	*endpoints;
};

struct raw_gadget_interface {
	struct raw_gadget_altsetting	*altsettings;
	int				num_altsettings;
	int				current_altsetting;
};

struct raw_gadget_config {
	struct usb_config_descriptor	config;
	struct raw_gadget_interface	*interfaces;
};

struct raw_gadget_device {
	struct usb_device_descriptor 	device;
	struct raw_gadget_config	*configs;
	int				current_config;
};

extern struct raw_gadget_device host_device_desc;

/*----------------------------------------------------------------------*/

enum usb_injection_flags {
	USB_INJECTION_FLAG_NONE,
	USB_INJECTION_FLAG_IGNORE,
	USB_INJECTION_FLAG_STALL,
};

/*----------------------------------------------------------------------*/

int usb_raw_open();
void usb_raw_init(int fd, enum usb_device_speed speed,
			const char *driver, const char *device);
void usb_raw_run(int fd);
void usb_raw_event_fetch(int fd, struct usb_raw_event *event);
int usb_raw_ep0_read(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep0_write(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep_enable(int fd, struct usb_endpoint_descriptor *desc);
int usb_raw_ep_disable(int fd, uint32_t num);
int usb_raw_ep_read(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep_write(int fd, struct usb_raw_ep_io *io);
void usb_raw_configure(int fd);
void usb_raw_vbus_draw(int fd, uint32_t power);
int usb_raw_eps_info(int fd, struct usb_raw_eps_info *info);
void usb_raw_ep0_stall(int fd);
void usb_raw_ep_set_halt(int fd, int ep);

void log_control_request(struct usb_ctrlrequest *ctrl);
void log_event(struct usb_raw_event *event);
void print_eps_info(int fd);
