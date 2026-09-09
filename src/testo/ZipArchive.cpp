#include "ZipArchive.hpp"


#ifndef __linux__
#include <stdexcept>
namespace zip_archive {
void create(const fs::path&, const fs::path&) { throw std::runtime_error("ZIP state containers are not implemented on this platform yet"); }
void extract(const fs::path&, const fs::path&) { throw std::runtime_error("ZIP state containers are not implemented on this platform yet"); }
}
#else

#include <archive.h>
#include <archive_entry.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zip_archive {
namespace {

struct ArchiveHandle {
    archive* value = nullptr;
    bool writer = false;
    ~ArchiveHandle() {
        if (!value) return;
        if (writer) archive_write_free(value);
        else archive_read_free(value);
    }
};

std::runtime_error archive_error(archive* a, const std::string& action) {
    const char* error = archive_error_string(a);
    return std::runtime_error(action + (error ? ": " + std::string(error) : std::string()));
}

fs::path safe_output_path(const fs::path& root, const char* pathname) {
    if (!pathname || !*pathname) throw std::runtime_error("ZIP entry has an empty path");
    fs::path rel(pathname);
    if (rel.is_absolute()) throw std::runtime_error("ZIP contains an absolute path: " + rel.generic_string());
    for (const auto& part: rel) {
        if (part == "..") throw std::runtime_error("ZIP entry escapes destination: " + rel.generic_string());
    }
    return root / rel;
}

} // namespace

void create(const fs::path& source_directory, const fs::path& destination_zip) {
    if (!fs::is_directory(source_directory)) {
        throw std::runtime_error("ZIP source is not a directory: " + source_directory.generic_string());
    }
    if (fs::exists(destination_zip)) {
        throw std::runtime_error("ZIP destination already exists: " + destination_zip.generic_string());
    }
    if (!destination_zip.parent_path().empty()) fs::create_directories(destination_zip.parent_path());

    ArchiveHandle handle;
    handle.value = archive_write_new();
    handle.writer = true;
    if (!handle.value) throw std::runtime_error("Can't allocate ZIP writer");
    if (archive_write_set_format_zip(handle.value) != ARCHIVE_OK) throw archive_error(handle.value, "Can't select ZIP format");
    archive_write_set_format_option(handle.value, "zip", "compression", "deflate");
    if (archive_write_open_filename(handle.value, destination_zip.generic_string().c_str()) != ARCHIVE_OK) {
        throw archive_error(handle.value, "Can't create ZIP archive");
    }

    std::vector<char> buffer(1024 * 1024);
    for (const auto& item: fs::recursive_directory_iterator(source_directory)) {
        if (!fs::is_regular_file(item.path())) continue;
        auto rel = fs::relative(item.path(), source_directory).generic_string();

        archive_entry* entry = archive_entry_new();
        if (!entry) throw std::runtime_error("Can't allocate ZIP entry");
        archive_entry_set_pathname(entry, rel.c_str());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, static_cast<la_int64_t>(fs::file_size(item.path())));

        if (archive_write_header(handle.value, entry) != ARCHIVE_OK) {
            archive_entry_free(entry);
            throw archive_error(handle.value, "Can't write ZIP entry header for " + rel);
        }

        std::ifstream input(item.path().generic_string(), std::ios::binary);
        if (!input) {
            archive_entry_free(entry);
            throw std::runtime_error("Can't read file for ZIP: " + item.path().generic_string());
        }
        while (input) {
            input.read(buffer.data(), buffer.size());
            auto count = input.gcount();
            if (count > 0 && archive_write_data(handle.value, buffer.data(), static_cast<size_t>(count)) < 0) {
                archive_entry_free(entry);
                throw archive_error(handle.value, "Can't write ZIP entry data for " + rel);
            }
        }
        archive_entry_free(entry);
    }

    if (archive_write_close(handle.value) != ARCHIVE_OK) throw archive_error(handle.value, "Can't finalize ZIP archive");
}

void extract(const fs::path& source_zip, const fs::path& destination_directory) {
    if (!fs::is_regular_file(source_zip)) {
        throw std::runtime_error("ZIP source does not exist: " + source_zip.generic_string());
    }
    if (fs::exists(destination_directory)) fs::remove_all(destination_directory);
    fs::create_directories(destination_directory);

    ArchiveHandle handle;
    handle.value = archive_read_new();
    if (!handle.value) throw std::runtime_error("Can't allocate ZIP reader");
    archive_read_support_filter_all(handle.value);
    archive_read_support_format_zip(handle.value);
    if (archive_read_open_filename(handle.value, source_zip.generic_string().c_str(), 1024 * 1024) != ARCHIVE_OK) {
        throw archive_error(handle.value, "Can't open ZIP archive");
    }

    std::vector<char> buffer(1024 * 1024);
    archive_entry* entry = nullptr;
    while (archive_read_next_header(handle.value, &entry) == ARCHIVE_OK) {
        auto output = safe_output_path(destination_directory, archive_entry_pathname(entry));
        auto type = archive_entry_filetype(entry);
        if (type == AE_IFDIR) {
            fs::create_directories(output);
            archive_read_data_skip(handle.value);
            continue;
        }
        if (type != AE_IFREG) {
            throw std::runtime_error("ZIP contains unsupported non-regular entry: " + output.generic_string());
        }
        if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
        std::ofstream out(output.generic_string(), std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Can't create extracted file: " + output.generic_string());
        while (true) {
            auto count = archive_read_data(handle.value, buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) throw archive_error(handle.value, "Can't extract ZIP entry " + output.generic_string());
            out.write(buffer.data(), count);
            if (!out) throw std::runtime_error("Can't write extracted file: " + output.generic_string());
        }
    }

    if (archive_errno(handle.value) != 0) throw archive_error(handle.value, "Can't finish reading ZIP archive");
    if (archive_read_close(handle.value) != ARCHIVE_OK) throw archive_error(handle.value, "Can't close ZIP archive");
}

} // namespace zip_archive

#endif
