#include <catch.hpp>

#ifdef __linux__
#include "../backends/qemu/QemuAddress.hpp"

#include <cstdlib>
#include <optional>
#include <string>

namespace {
struct EnvRestore {
	std::optional<std::string> old;
	EnvRestore() {
		if (const char* value = std::getenv("TESTO_QEMU_ADDRESS")) old = value;
	}
	~EnvRestore() {
		if (old) setenv("TESTO_QEMU_ADDRESS", old->c_str(), 1);
		else unsetenv("TESTO_QEMU_ADDRESS");
	}
};
}
TEST_CASE("QEMU address environment override") {
	EnvRestore restore;

	unsetenv("TESTO_QEMU_ADDRESS");
	REQUIRE(qemu_network_uri() == "qemu:///system");
	REQUIRE(qemu_domain_uri(false) == "qemu:///system");
	REQUIRE(qemu_domain_uri(true) == "qemu:///session");

	setenv("TESTO_QEMU_ADDRESS", "test:///default", 1);
	REQUIRE(qemu_network_uri() == "test:///default");
	REQUIRE(qemu_domain_uri(false) == "test:///default");
	REQUIRE(qemu_domain_uri(true) == "qemu:///session");

	setenv("TESTO_QEMU_ADDRESS", "", 1);
	REQUIRE(qemu_network_uri().empty());
	REQUIRE(qemu_domain_uri(false).empty());
	REQUIRE(qemu_domain_uri(true) == "qemu:///session");
}
#endif
