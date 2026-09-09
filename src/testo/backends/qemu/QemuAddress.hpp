#pragma once

#include <cstdlib>
#include <string>

inline std::string qemu_network_uri() {
	const char* address = std::getenv("TESTO_QEMU_ADDRESS");
	return address ? std::string(address) : "qemu:///system";
}

inline std::string qemu_domain_uri(bool user_mode) {
	return user_mode ? "qemu:///session" : qemu_network_uri();
}
