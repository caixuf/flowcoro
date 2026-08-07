#define ERROR 0x7FFFFFFF
#include "flowcoro/net.h"

#include <cstdint>

#ifndef ERROR
#error "ERROR macro should remain available after including flowcoro/net.h"
#endif

int main() {
    return static_cast<uint32_t>(flowcoro::net::IO_EVENT_ERROR) == 0x04 ? 0 : 1;
}
