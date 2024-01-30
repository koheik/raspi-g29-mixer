#include "input-device.h"
#include <vector>

void ep0_loop(int fd, std::vector<InputDevice *> *trims);
