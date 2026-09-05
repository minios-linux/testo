#include "StateTransfer.hpp"


#ifndef __linux__
#include <stdexcept>
namespace state_transfer {
void export_state(const IR::Program&, const fs::path&, bool) {
    throw std::runtime_error("Testo state transfer is not implemented for this hypervisor backend yet");
}
void export_test_state(const std::shared_ptr<IR::Test>&, const fs::path&, bool, bool) {
    throw std::runtime_error("Testo state transfer is not implemented for this hypervisor backend yet");
}
void import_state(const fs::path&, bool, bool) {
    throw std::runtime_error("Testo state transfer is not implemented for this hypervisor backend yet");
}
}
#else
#include "ZipArchive.hpp"
#include "Utils.hpp"

#include "IR/Program.hpp"
#include "IR/Machine.hpp"
#include "IR/Network.hpp"
#include "IR/FlashDrive.hpp"
#include "backends/Environment.hpp"
#include <qemu/Connect.hpp>
#include <nlohmann/json.hpp>
#include <pugixml/pugixml.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace state_transfer {
namespace {

using json = nlohmann::json;

std::string qemu_uri(bool user_mode) {
    return user_mode ? "qemu:///session" : "qemu:///system";
}

std::string timestamp_utc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

void ensure_parent(const fs::path& path) {
    auto parent = path.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
}

void copy_file_checked(const fs::path& source, const fs::path& destination, bool overwrite = false) {
    if (!fs::is_regular_file(source)) {
        throw std::runtime_error("Source file does not exist: " + source.generic_string());
    }
    ensure_parent(destination);
    auto options = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    if (!fs::copy_file(source, destination, options)) {
        throw std::runtime_error("Can't copy " + source.generic_string() + " to " + destination.generic_string());
    }
}

bool files_equal(const fs::path& a, const fs::path& b) {
    if (!fs::is_regular_file(a) || !fs::is_regular_file(b)) return false;
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream as(a.generic_string(), std::ios::binary);
    std::ifstream bs(b.generic_string(), std::ios::binary);
    char abuf[1024 * 1024], bbuf[1024 * 1024];
    while (as && bs) {
        as.read(abuf, sizeof(abuf));
        bs.read(bbuf, sizeof(bbuf));
        auto n = as.gcount();
        if (n != bs.gcount() || !std::equal(abuf, abuf + n, bbuf)) return false;
    }
    return true;
}

void save_xml(const pugi::xml_document& doc, const fs::path& path) {
    ensure_parent(path);
    if (!doc.save_file(path.generic_string().c_str(), "\t")) {
        throw std::runtime_error("Can't write XML file: " + path.generic_string());
    }
}

json read_json(const fs::path& path) {
    std::ifstream is(path.generic_string());
    if (!is) throw std::runtime_error("Can't open JSON file: " + path.generic_string());
    json result;
    is >> result;
    return result;
}

void write_json(const fs::path& path, const json& value) {
    ensure_parent(path);
    std::ofstream os(path.generic_string());
    if (!os) throw std::runtime_error("Can't write JSON file: " + path.generic_string());
    os << value.dump(4) << '\n';
}

fs::path checked_container_path(const fs::path& root, const std::string& value) {
    fs::path rel(value);
    if (rel.empty() || rel.is_absolute()) {
        throw std::runtime_error("Invalid absolute/empty container path: " + value);
    }
    for (const auto& part: rel) {
        if (part == "..") throw std::runtime_error("Container path escapes root: " + value);
    }
    return root / rel;
}

void copy_metadata_dir(const fs::path& source_dir, const fs::path& destination_dir,
                       const std::string& manifest_name, json& out) {
    if (!fs::is_directory(source_dir)) return;
    for (const auto& entry: fs::directory_iterator(source_dir)) {
        if (!fs::is_regular_file(entry.path())) continue;
        auto dst = destination_dir / entry.path().filename();
        copy_file_checked(entry.path(), dst);
        out.push_back({{"name", manifest_name}, {"path", dst.generic_string()}});
    }
}

void make_paths_relative(json& manifest, const fs::path& root) {
    auto relative = [&](json& item) {
        fs::path p(item.at("path").get<std::string>());
        item["path"] = fs::relative(p, root).generic_string();
    };
    for (auto& item: manifest["machines"]) relative(item);
    for (auto& item: manifest["machine_snapshots"]) relative(item);
    for (auto& item: manifest["networks"]) relative(item);
    for (auto& item: manifest["flash_drives"]) relative(item);
    for (auto& group: {"storage", "external", "user"}) {
        for (auto& item: manifest["disks"][group]) relative(item);
    }
    for (auto& item: manifest["metadata"]["machines"]) relative(item);
    for (auto& item: manifest["metadata"]["flash_drives"]) relative(item);
}

void rewrite_storage_sources(pugi::xml_node node, const std::map<std::string, fs::path>& storage) {
    if (std::string(node.name()) == "source" && node.attribute("file")) {
        fs::path old(node.attribute("file").value());
        auto it = storage.find(old.filename().generic_string());
        if (it != storage.end()) node.attribute("file").set_value(it->second.generic_string().c_str());
    }
    for (auto child: node.children()) rewrite_storage_sources(child, storage);
}

bool domain_exists(vir::Connect& conn, const std::string& name) {
    for (auto& domain: conn.domains()) if (domain.name() == name) return true;
    return false;
}

bool network_exists(vir::Connect& conn, const std::string& name) {
    for (auto& network: conn.networks()) if (network.name() == name) return true;
    return false;
}

void remove_domain(vir::Connect& conn, const std::string& name) {
    if (!domain_exists(conn, name)) return;
    auto domain = conn.domain_lookup_by_name(name);
    if (domain.is_active()) domain.stop();
    for (auto& snap: domain.snapshots()) snap.destroy({VIR_DOMAIN_SNAPSHOT_DELETE_CHILDREN});
    domain.undefine();
}

void remove_network(vir::Connect& conn, const std::string& name) {
    if (!network_exists(conn, name)) return;
    auto network = conn.network_lookup_by_name(name);
    if (network.is_active()) network.stop();
    if (network.is_persistent()) network.undefine();
}

struct TempDirectory {
    fs::path path;
    TempDirectory(const std::string& purpose) {
        path = fs::temp_directory_path() / ("testo-" + purpose + "-" + generate_uuid_v4());
        fs::create_directories(path);
    }
    ~TempDirectory() {
        try { if (!path.empty() && fs::exists(path)) fs::remove_all(path); } catch (...) {}
    }
};

fs::path sibling_temp_path(const fs::path& destination, const std::string& purpose) {
    auto parent = destination.parent_path();
    if (parent.empty()) parent = fs::current_path();
    fs::create_directories(parent);
    return parent / ("." + destination.filename().generic_string() + ".testo-" + purpose + "-" + generate_uuid_v4());
}

void replace_destination(const fs::path& staged, const fs::path& destination) {
    auto backup = sibling_temp_path(destination, "backup");
    bool had_destination = fs::exists(destination);
    if (had_destination) fs::rename(destination, backup);
    try {
        fs::rename(staged, destination);
    } catch (...) {
        try { if (had_destination && fs::exists(backup)) fs::rename(backup, destination); } catch (...) {}
        throw;
    }
    if (had_destination && fs::exists(backup)) fs::remove_all(backup);
}

} // namespace

void export_directory(const std::vector<std::shared_ptr<IR::Test>>& tests, const fs::path& destination, bool user_mode) {
    std::set<std::shared_ptr<IR::Machine>> machines;
    std::set<std::shared_ptr<IR::Network>> networks;
    std::set<std::shared_ptr<IR::FlashDrive>> flash_drives;
    for (const auto& test: tests) {
        auto ms = test->get_all_machines(); machines.insert(ms.begin(), ms.end());
        auto ns = test->get_all_networks(); networks.insert(ns.begin(), ns.end());
        auto fsd = test->get_all_flash_drives(); flash_drives.insert(fsd.begin(), fsd.end());
    }
    if (fs::exists(destination) && (!fs::is_directory(destination) || !fs::is_empty(destination))) {
        throw std::runtime_error("Export destination already exists and is not empty: " + destination.generic_string());
    }
    fs::create_directories(destination);

    json manifest = {
        {"version", 1}, {"export_timestamp", timestamp_utc()},
        {"machines", json::array()}, {"machine_snapshots", json::array()},
        {"networks", json::array()}, {"flash_drives", json::array()},
        {"metadata", {{"machines", json::array()}, {"flash_drives", json::array()}}},
        {"disks", {{"storage", json::array()}, {"external", json::array()}, {"user", json::array()}}}
    };

    vir::Connect conn(virConnectOpen(qemu_uri(user_mode).c_str()));
    if (!conn.handle) throw std::runtime_error("Can't connect to QEMU for export");
    std::set<std::string> copied_storage, copied_external;

    for (const auto& machine: machines) {
        const auto id = machine->vm()->id();
        if (!machine->is_defined() || !domain_exists(conn, id)) {
            throw std::runtime_error("Can't export undefined virtual machine: " + id);
        }
        auto domain = conn.domain_lookup_by_name(id);
        auto machine_path = destination / "machines" / (id + ".xml");
        auto domain_xml = domain.dump_xml();
        save_xml(domain_xml, machine_path);
        manifest["machines"].push_back({{"path", machine_path.generic_string()}});

        for (auto& snap: domain.snapshots()) {
            auto snap_path = destination / "machine_snapshots" / id / (snap.name() + ".xml");
            save_xml(snap.dump_xml(), snap_path);
            manifest["machine_snapshots"].push_back({{"name", id}, {"path", snap_path.generic_string()}});
        }

        copy_metadata_dir(env->vm_metadata_dir() / id, destination / "metadata/machines" / id,
                          id, manifest["metadata"]["machines"]);

        auto main_metadata = env->vm_metadata_dir() / id / id;
        auto vm_config = IR::Machine::read_config_from_metadata(main_metadata);
        if (vm_config.count("iso")) {
            fs::path source(vm_config.at("iso").at("source").get<std::string>());
            source = fs::canonical(source);
            auto key = source.generic_string();
            if (copied_external.insert(key).second) {
                auto dst = destination / "disks/external" / source.relative_path();
                copy_file_checked(source, dst);
                manifest["disks"]["external"].push_back({{"path", dst.generic_string()}});
            }
        }

        auto devices = domain_xml.first_child().child("devices");
        for (auto disk = devices.child("disk"); disk; disk = disk.next_sibling("disk")) {
            if (std::string(disk.attribute("device").value()) != "disk") continue;
            auto source = disk.child("source").attribute("file");
            if (!source) continue;
            fs::path src(source.value());
            auto key = src.generic_string();
            if (copied_storage.insert(key).second) {
                auto dst = destination / "disks/storage" / src.filename();
                copy_file_checked(src, dst);
                manifest["disks"]["storage"].push_back({{"path", dst.generic_string()}});
            }
        }
    }


    for (const auto& network: networks) {
        const auto id = network->nw()->id();
        auto source = env->network_metadata_dir() / id / id;
        if (!fs::is_regular_file(source)) {
            throw std::runtime_error("Missing network metadata: " + source.generic_string());
        }
        auto dst = destination / "networks" / id;
        copy_file_checked(source, dst);
        manifest["networks"].push_back({{"path", dst.generic_string()}});
    }

    for (const auto& flash: flash_drives) {
        const auto id = flash->fd()->id();
        auto src = flash->fd()->img_path();
        auto dst = destination / "flash_drives" / (id + ".img");
        copy_file_checked(src, dst);
        manifest["flash_drives"].push_back({{"path", dst.generic_string()}});
        copy_metadata_dir(env->flash_drives_metadata_dir() / id,
                          destination / "metadata/flash_drives" / id,
                          id, manifest["metadata"]["flash_drives"]);
    }

    make_paths_relative(manifest, destination);
    write_json(destination / "manifest.json", manifest);
}

void import_directory(const fs::path& source, bool force, bool user_mode) {
    if (!fs::is_directory(source)) throw std::runtime_error("Import source is not a directory: " + source.generic_string());
    auto manifest_path = source / "manifest.json";
    auto manifest = read_json(manifest_path);
    if (manifest.value("version", 0) != 1) throw std::runtime_error("Unsupported Testo state container version");
    for (const auto& key: {"machines", "machine_snapshots", "networks", "flash_drives", "metadata", "disks"}) {
        if (!manifest.count(key)) throw std::runtime_error(std::string("Manifest is missing field: ") + key);
    }
    env->prepare();
    vir::Connect conn(virConnectOpen(qemu_uri(user_mode).c_str()));
    if (!conn.handle) throw std::runtime_error("Can't connect to QEMU for import");
    vir::Connect network_conn(virConnectOpen("qemu:///system"));
    if (!network_conn.handle) throw std::runtime_error("Can't connect to system QEMU for network import");

    const auto storage_dir = env->testo_dir() / "testo-storage-pool";
    const auto flash_dir = env->testo_dir() / "testo-flash-drives-pool";
    std::map<std::string, fs::path> storage_map;
    struct CopyPlan { fs::path src, dst; bool identical = false; };
    std::vector<CopyPlan> copies;

    auto plan_copy = [&](const std::string& rel, const fs::path& dst) {
        auto src = checked_container_path(source, rel);
        if (!fs::is_regular_file(src)) throw std::runtime_error("Container file is missing: " + rel);
        bool identical = fs::is_regular_file(dst) && files_equal(src, dst);
        if (fs::exists(dst) && !identical && !force) {
            throw std::runtime_error("Destination already exists: " + dst.generic_string() + ". Use --force to overwrite it.");
        }
        copies.push_back({src, dst, identical});
    };

    for (const auto& item: manifest["disks"]["storage"]) {
        auto rel = item.at("path").get<std::string>();
        auto src = checked_container_path(source, rel);
        auto dst = storage_dir / src.filename();
        storage_map[src.filename().generic_string()] = dst;
        plan_copy(rel, dst);
    }
    for (const auto& item: manifest["disks"]["external"]) {
        auto rel = fs::path(item.at("path").get<std::string>());
        auto prefix = fs::path("disks/external");
        auto inner = fs::relative(rel, prefix);
        if (inner.empty() || inner.generic_string().find("..") == 0) throw std::runtime_error("Invalid external disk path: " + rel.generic_string());
        plan_copy(rel.generic_string(), fs::path("/") / inner);
    }
    if (!manifest["disks"]["user"].empty()) {
        throw std::runtime_error("Container uses disks.user, which is not supported yet");
    }

    std::set<std::string> machine_names;
    for (const auto& item: manifest["machines"]) {
        auto xml_path = checked_container_path(source, item.at("path").get<std::string>());
        pugi::xml_document doc;
        if (!doc.load_file(xml_path.generic_string().c_str())) throw std::runtime_error("Invalid machine XML: " + xml_path.generic_string());
        auto name = std::string(doc.child("domain").child("name").text().as_string());
        if (name.empty()) throw std::runtime_error("Machine XML has no name: " + xml_path.generic_string());
        machine_names.insert(name);
        if (domain_exists(conn, name) && !force) throw std::runtime_error("Virtual machine already exists: " + name);
    }

    std::set<std::string> network_names;
    for (const auto& item: manifest["networks"]) {
        auto rel = item.at("path").get<std::string>();
        auto metadata_path = checked_container_path(source, rel);
        auto metadata = read_json(metadata_path);
        if (!metadata.count("network_config") || !metadata.at("network_config").is_string()) {
            throw std::runtime_error("Network container entry has no network_config: " + rel);
        }
        auto cfg = json::parse(metadata.at("network_config").get<std::string>());
        auto id = cfg.at("prefix").get<std::string>() + cfg.at("name").get<std::string>();
        network_names.insert(id);
        if (network_exists(network_conn, id) && !force) {
            throw std::runtime_error("Virtual network already exists: " + id);
        }
        plan_copy(rel, env->network_metadata_dir() / id / id);
    }

    for (const auto& item: manifest["flash_drives"]) {
        auto rel = item.at("path").get<std::string>();
        auto src = checked_container_path(source, rel);
        plan_copy(rel, flash_dir / src.filename());
    }

    for (const auto& item: manifest["metadata"]["machines"]) {
        auto rel = item.at("path").get<std::string>();
        auto name = item.at("name").get<std::string>();
        auto src = checked_container_path(source, rel);
        plan_copy(rel, env->vm_metadata_dir() / name / src.filename());
    }
    for (const auto& item: manifest["metadata"]["flash_drives"]) {
        auto rel = item.at("path").get<std::string>();
        auto name = item.at("name").get<std::string>();
        auto src = checked_container_path(source, rel);
        plan_copy(rel, env->flash_drives_metadata_dir() / name / src.filename());
    }

    // All file and libvirt conflicts have now been checked. No state was modified above.
    if (force) {
        for (const auto& name: machine_names) remove_domain(conn, name);
        for (const auto& name: network_names) remove_network(network_conn, name);
    }
    for (const auto& plan: copies) {
        if (plan.identical) continue;
        copy_file_checked(plan.src, plan.dst, force);
    }
    try { conn.storage_pool_lookup_by_name("testo-storage-pool").refresh(); } catch (...) {}
    try { conn.storage_pool_lookup_by_name("testo-flash-drives-pool").refresh(); } catch (...) {}

    for (const auto& item: manifest["networks"]) {
        auto metadata = read_json(checked_container_path(source, item.at("path").get<std::string>()));
        auto cfg = json::parse(metadata.at("network_config").get<std::string>());
        auto network = env->create_network(cfg);
        network->create();
    }

    for (const auto& item: manifest["machines"]) {
        auto xml_path = checked_container_path(source, item.at("path").get<std::string>());
        pugi::xml_document doc;
        if (!doc.load_file(xml_path.generic_string().c_str())) throw std::runtime_error("Invalid machine XML: " + xml_path.generic_string());
        rewrite_storage_sources(doc, storage_map);
        conn.domain_define_xml(doc);
    }

    struct SnapshotItem { std::string machine, name, parent; fs::path path; };
    std::vector<SnapshotItem> snapshots;
    for (const auto& item: manifest["machine_snapshots"]) {
        auto path = checked_container_path(source, item.at("path").get<std::string>());
        pugi::xml_document doc;
        if (!doc.load_file(path.generic_string().c_str())) throw std::runtime_error("Invalid snapshot XML: " + path.generic_string());
        snapshots.push_back({item.at("name").get<std::string>(), doc.child("domainsnapshot").child("name").text().as_string(),
            doc.child("domainsnapshot").child("parent").child("name").text().as_string(), path});
    }
    std::set<std::pair<std::string, std::string>> restored;
    size_t remaining = snapshots.size();
    while (remaining) {
        bool progress = false;
        for (const auto& item: snapshots) {
            auto key = std::make_pair(item.machine, item.name);
            if (restored.count(key)) continue;
            if (!item.parent.empty() && !restored.count({item.machine, item.parent})) continue;
            pugi::xml_document doc;
            doc.load_file(item.path.generic_string().c_str());
            rewrite_storage_sources(doc, storage_map);
            auto domain = conn.domain_lookup_by_name(item.machine);
            domain.snapshot_create_xml(doc, {VIR_DOMAIN_SNAPSHOT_CREATE_REDEFINE});
            restored.insert(key);
            --remaining;
            progress = true;
        }
        if (!progress) throw std::runtime_error("Snapshot parent graph in container is inconsistent");
    }

    std::cout << "Testo state restored successfully" << std::endl;
}

void export_tests_state(const std::vector<std::shared_ptr<IR::Test>>& tests, const fs::path& destination, bool user_mode, bool replace_existing) {
    if (!replace_existing) {
        if (destination.extension() != ".zip") {
            export_directory(tests, destination, user_mode);
            return;
        }
        if (fs::exists(destination)) {
            throw std::runtime_error("Export destination already exists: " + destination.generic_string());
        }
        TempDirectory temp("export");
        export_directory(tests, temp.path, user_mode);
        zip_archive::create(temp.path, destination);
        return;
    }

    if (destination.extension() == ".zip") {
        TempDirectory temp("export");
        export_directory(tests, temp.path, user_mode);
        auto staged_zip = sibling_temp_path(destination, "export");
        try {
            zip_archive::create(temp.path, staged_zip);
            replace_destination(staged_zip, destination);
        } catch (...) {
            try { if (fs::exists(staged_zip)) fs::remove(staged_zip); } catch (...) {}
            throw;
        }
        return;
    }

    auto staged_dir = sibling_temp_path(destination, "export");
    try {
        export_directory(tests, staged_dir, user_mode);
        replace_destination(staged_dir, destination);
    } catch (...) {
        try { if (fs::exists(staged_dir)) fs::remove_all(staged_dir); } catch (...) {}
        throw;
    }
}

void export_state(const IR::Program& program, const fs::path& destination, bool user_mode) {
    export_tests_state(program.all_selected_tests, destination, user_mode, false);
}

void export_test_state(const std::shared_ptr<IR::Test>& test, const fs::path& destination, bool user_mode, bool replace_destination) {
    export_tests_state({test}, destination, user_mode, replace_destination);
}

void import_state(const fs::path& source, bool force, bool user_mode) {
    if (fs::is_directory(source)) {
        import_directory(source, force, user_mode);
        return;
    }
    if (!fs::is_regular_file(source) || source.extension() != ".zip") {
        throw std::runtime_error("Import source must be a Testo state directory or .zip archive: " + source.generic_string());
    }
    TempDirectory temp("import");
    zip_archive::extract(source, temp.path);
    import_directory(temp.path, force, user_mode);
}

} // namespace state_transfer

#endif
