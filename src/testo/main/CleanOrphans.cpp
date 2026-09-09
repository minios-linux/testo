#include "CleanOrphans.hpp"

#ifdef __linux__

#include "../backends/Environment.hpp"
#include "../backends/qemu/QemuAddress.hpp"
#include <qemu/Connect.hpp>

#include <algorithm>
#include <iostream>
#include <set>
#include <vector>

namespace {

bool item_is_selected(const CleanModeArgs& args, const std::string& name) {
	return args.items.empty() ||
		std::find(args.items.begin(), args.items.end(), name) != args.items.end();
}

bool id_is_selected(const CleanModeArgs& args, const std::string& id) {
	if (id.compare(0, args.prefix.size(), args.prefix) != 0) return false;
	return item_is_selected(args, id.substr(args.prefix.size()));
}

struct OrphanState {
	std::set<std::string> vm_ids, network_ids, flash_ids;
	std::set<std::string> storage_volumes, flash_volumes;
	std::vector<fs::path> vm_metadata, network_metadata, flash_metadata;
};

void collect_metadata_dirs(const fs::path& root, const CleanModeArgs& args,
	std::vector<fs::path>& paths, std::set<std::string>& ids)
{
	if (!fs::is_directory(root)) return;
	for (const auto& entry: fs::directory_iterator(root)) {
		if (!fs::is_directory(entry.path())) continue;
		const auto id = entry.path().filename().generic_string();
		if (!id_is_selected(args, id)) continue;
		paths.push_back(entry.path());
		ids.insert(id);
	}
}

bool domain_is_testo_related(const vir::Domain& domain) {
	auto xml = domain.dump_xml();
	auto metadata = xml.first_child().child("metadata");
	return metadata && metadata.child("testo:is_testo_related");
}

bool network_looks_testo_related(const vir::Network& network) {
	auto xml = network.dump_xml();
	auto root = xml.first_child();
	const std::string name = root.child("name").text().as_string();
	const std::string bridge = root.child("bridge").attribute("name").value();
	return !name.empty() && name == network.name() && bridge == name;
}

std::string normalized_file_path(const fs::path& path) {
	try {
		if (fs::exists(path)) return fs::canonical(path).generic_string();
	} catch (const std::exception&) {
	}
	return path.generic_string();
}

void collect_domain_sources(vir::Connect& conn, std::set<std::string>& sources) {
	for (auto& domain: conn.domains()) {
		auto xml = domain.dump_xml();
		auto devices = xml.first_child().child("devices");
		for (auto disk = devices.child("disk"); disk; disk = disk.next_sibling("disk")) {
			auto file = disk.child("source").attribute("file");
			if (file && *file.value()) sources.insert(normalized_file_path(file.value()));
		}
	}
}

void collect_storage_volumes(vir::Connect& conn, const CleanModeArgs& args,
	OrphanState& state)
{
	try {
		auto pool = conn.storage_pool_lookup_by_name("testo-storage-pool");
		for (auto& volume: pool.volumes()) {
			const auto name = volume.name();
			const auto at = name.find('@');
			if (at == std::string::npos || at == 0) continue;
			const auto vm_id = name.substr(0, at);
			if (id_is_selected(args, vm_id)) state.storage_volumes.insert(name);
		}
	} catch (const std::exception&) {
	}

	try {
		auto pool = conn.storage_pool_lookup_by_name("testo-flash-drives-pool");
		for (auto& volume: pool.volumes()) {
			const auto name = volume.name();
			if (name.size() <= 4 || name.substr(name.size() - 4) != ".img") continue;
			const auto flash_id = name.substr(0, name.size() - 4);
			if (id_is_selected(args, flash_id)) state.flash_volumes.insert(name);
		}
	} catch (const std::exception&) {
	}
}

bool orphan_state_empty(const OrphanState& state) {
	return state.vm_ids.empty() && state.network_ids.empty() && state.flash_ids.empty() &&
		state.storage_volumes.empty() && state.flash_volumes.empty();
}

void confirm_orphan_cleanup(const CleanModeArgs& args, const OrphanState& state) {
	if (args.assume_yes || orphan_state_empty(state)) return;
	std::cout << "Testo found orphaned state matching the clean selection:\n";
	for (const auto& id: state.vm_ids) std::cout << "\tVM state: " << id << '\n';
	for (const auto& id: state.network_ids) std::cout << "\tNetwork state: " << id << '\n';
	for (const auto& id: state.flash_ids) std::cout << "\tFlash state: " << id << '\n';
	for (const auto& name: state.storage_volumes) std::cout << "\tStorage volume: " << name << '\n';
	for (const auto& name: state.flash_volumes) std::cout << "\tFlash volume: " << name << '\n';
	std::cout << "\nDo you confirm erasing these orphaned entities? [y/N]: ";
	std::string choice;
	std::getline(std::cin, choice);
	std::transform(choice.begin(), choice.end(), choice.begin(), ::toupper);
	if (choice != "Y" && choice != "YES") throw std::runtime_error("Aborted");
}

void remove_testo_domains(vir::Connect& conn, const CleanModeArgs& args) {
	for (auto& domain: conn.domains()) {
		const auto id = domain.name();
		if (!id_is_selected(args, id) || !domain_is_testo_related(domain)) continue;
		try {
			if (domain.is_active()) domain.stop();
			for (auto& snapshot: domain.snapshots({VIR_DOMAIN_SNAPSHOT_LIST_ROOTS})) {
				snapshot.destroy({VIR_DOMAIN_SNAPSHOT_DELETE_CHILDREN});
			}
			domain.undefine();
			std::cout << "Deleted orphaned Testo virtual machine " << id << std::endl;
		} catch (const std::exception& error) {
			std::cerr << "Couldn't remove orphaned Testo virtual machine " << id << std::endl;
			std::cerr << error.what() << std::endl;
		}
	}
}

void remove_testo_networks(vir::Connect& conn, const CleanModeArgs& args,
	const std::set<std::string>& metadata_ids)
{
	for (auto& network: conn.networks()) {
		const auto id = network.name();
		if (!id_is_selected(args, id) || !metadata_ids.count(id)) continue;
		if (!network_looks_testo_related(network)) continue;
		try {
			if (network.is_active()) network.stop();
			if (network.is_persistent()) network.undefine();
			std::cout << "Deleted orphaned Testo network " << id << std::endl;
		} catch (const std::exception& error) {
			std::cerr << "Couldn't remove orphaned Testo network " << id << std::endl;
			std::cerr << error.what() << std::endl;
		}
	}
}

bool contains_domain(vir::Connect& conn, const std::string& id) {
	for (auto& domain: conn.domains()) if (domain.name() == id) return true;
	return false;
}

bool contains_network(vir::Connect& conn, const std::string& id) {
	for (auto& network: conn.networks()) if (network.name() == id) return true;
	return false;
}

void remove_unreferenced_volumes(vir::Connect& conn, const OrphanState& state) {
	std::set<std::string> referenced;
	collect_domain_sources(conn, referenced);

	auto erase_from_pool = [&](const std::string& pool_name,
		const std::set<std::string>& names, const char* kind) {
		try {
			auto pool = conn.storage_pool_lookup_by_name(pool_name);
			const auto pool_path = pool.path();
			for (const auto& name: names) {
				const auto path = normalized_file_path(pool_path / name);
				if (referenced.count(path)) {
					std::cerr << "Keeping orphaned Testo " << kind << " volume " << name
						<< " because a virtual machine still references it" << std::endl;
					continue;
				}
				try {
					pool.storage_volume_lookup_by_name(name).erase();
					std::cout << "Deleted orphaned Testo " << kind << " volume " << name << std::endl;
				} catch (const std::exception& error) {
					std::cerr << "Couldn't remove orphaned Testo " << kind << " volume " << name << std::endl;
					std::cerr << error.what() << std::endl;
				}
			}
			pool.refresh();
		} catch (const std::exception&) {
		}
	};

	erase_from_pool("testo-storage-pool", state.storage_volumes, "storage");
	erase_from_pool("testo-flash-drives-pool", state.flash_volumes, "flash");
}

std::set<std::string> current_volume_names(vir::Connect& conn, const std::string& pool_name) {
	std::set<std::string> result;
	try {
		auto pool = conn.storage_pool_lookup_by_name(pool_name);
		for (auto& volume: pool.volumes()) result.insert(volume.name());
	} catch (const std::exception&) {
	}
	return result;
}

bool has_vm_volume(const std::set<std::string>& volumes, const std::string& id) {
	const std::string prefix = id + "@";
	for (const auto& name: volumes) {
		if (name.compare(0, prefix.size(), prefix) == 0) return true;
	}
	return false;
}

void remove_vm_flash_metadata(vir::Connect& conn, const OrphanState& state) {
	const auto storage = current_volume_names(conn, "testo-storage-pool");
	const auto flash = current_volume_names(conn, "testo-flash-drives-pool");

	for (const auto& path: state.vm_metadata) {
		const auto id = path.filename().generic_string();
		if (contains_domain(conn, id) || has_vm_volume(storage, id)) continue;
		try {
			fs::remove_all(path);
			std::cout << "Deleted orphaned Testo VM metadata " << id << std::endl;
		} catch (const std::exception& error) {
			std::cerr << "Couldn't remove orphaned Testo VM metadata " << id << ": " << error.what() << std::endl;
		}
	}

	for (const auto& path: state.flash_metadata) {
		const auto id = path.filename().generic_string();
		if (flash.count(id + ".img")) continue;
		try {
			fs::remove_all(path);
			std::cout << "Deleted orphaned Testo flash metadata " << id << std::endl;
		} catch (const std::exception& error) {
			std::cerr << "Couldn't remove orphaned Testo flash metadata " << id << ": " << error.what() << std::endl;
		}
	}
}

void remove_network_metadata(vir::Connect& conn, const OrphanState& state) {
	for (const auto& path: state.network_metadata) {
		const auto id = path.filename().generic_string();
		if (contains_network(conn, id)) continue;
		try {
			fs::remove_all(path);
			std::cout << "Deleted orphaned Testo network metadata " << id << std::endl;
		} catch (const std::exception& error) {
			std::cerr << "Couldn't remove orphaned Testo network metadata " << id << ": " << error.what() << std::endl;
		}
	}
}

} // namespace

void cleanup_orphaned_qemu_state(const CleanModeArgs& args) {
	OrphanState state;
	collect_metadata_dirs(env->vm_metadata_dir(), args, state.vm_metadata, state.vm_ids);
	collect_metadata_dirs(env->network_metadata_dir(), args, state.network_metadata, state.network_ids);
	collect_metadata_dirs(env->flash_drives_metadata_dir(), args, state.flash_metadata, state.flash_ids);

	vir::Connect domain_conn(virConnectOpen(qemu_domain_uri(args.user_mode).c_str()));
	if (!domain_conn.handle) throw std::runtime_error("Can't connect to QEMU while cleaning orphaned state");

	for (auto& domain: domain_conn.domains()) {
		if (id_is_selected(args, domain.name()) && domain_is_testo_related(domain)) {
			state.vm_ids.insert(domain.name());
		}
	}
	collect_storage_volumes(domain_conn, args, state);

	try {
		vir::Connect network_conn(virConnectOpen(qemu_network_uri().c_str()));
		if (network_conn.handle) {
			for (auto& network: network_conn.networks()) {
				if (!id_is_selected(args, network.name()) || !state.network_ids.count(network.name())) continue;
				if (network_looks_testo_related(network)) state.network_ids.insert(network.name());
			}
		}
	} catch (const std::exception&) {
	}

	if (orphan_state_empty(state)) return;
	confirm_orphan_cleanup(args, state);

	remove_testo_domains(domain_conn, args);
	try {
		vir::Connect network_conn(virConnectOpen(qemu_network_uri().c_str()));
		if (network_conn.handle) {
			remove_testo_networks(network_conn, args, state.network_ids);
			remove_network_metadata(network_conn, state);
		}
	} catch (const std::exception& error) {
		std::cerr << "Couldn't clean orphaned Testo network state: " << error.what() << std::endl;
	}

	remove_unreferenced_volumes(domain_conn, state);
	remove_vm_flash_metadata(domain_conn, state);
}

#else

void cleanup_orphaned_qemu_state(const CleanModeArgs&) {
}

#endif
