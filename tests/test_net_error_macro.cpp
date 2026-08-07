#define ERROR 0x7FFFFFFF
#include "flowcoro/net.h"

#include <cstdint>

#ifdef ERROR
#error "ERROR macro should be undefined by flowcoro/net.h to avoid IoEvent::ERROR conflicts"
#endif

int main() {
    return static_cast<uint32_t>(flowcoro::net::IoEvent::ERROR) == 0x04 ? 0 : 1;
}
