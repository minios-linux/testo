
#pragma once

#include "../Environment.hpp"
#include <qemu/Connect.hpp>
#include <cstdlib>
#include <stdexcept>

struct QemuEnvironment : public Environment {

	QemuEnvironment(bool user_mode = false);
	~QemuEnvironment();

	fs::path testo_dir() const override {
		if (!user_mode) {
			return "/var/lib/libvirt/testo";
		}
		const char* home = std::getenv("HOME");
		if (!home) {
			throw std::runtime_error("HOME is not set");
		}
		return fs::path(home) / ".local/share/libvirt/testo";
	}

	void prepare() override;
	void setup(const EnvironmentConfig& config) override;

	std::string hypervisor() const override {
		return "qemu";
	}

	std::shared_ptr<VM> create_vm(const nlohmann::json& config) override;
	std::shared_ptr<FlashDrive> create_flash_drive(const nlohmann::json& config) override;
	std::shared_ptr<Network> create_network(const nlohmann::json& config) override;

	void validate_vm_config(const nlohmann::json& config) override;
	void validate_flash_drive_config(const nlohmann::json& config) override;
	void validate_network_config(const nlohmann::json& config) override;

private:
	void prepare_storage_pool(const std::string& pool_name);
	bool user_mode = false;
	std::string qemu_uri;
	vir::Connect qemu_connect;
};
