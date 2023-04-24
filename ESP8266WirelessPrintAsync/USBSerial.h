#ifndef VCP_H
#define VCP_H

#include <cstddef>

namespace USBHost {
void setup(bool (*cb)(const uint8_t*, std::size_t, void*));
void loop();
void send(uint8_t* data, std::size_t len); //blocking?
bool is_connected();
}

#endif // VCP_H
