#pragma once

#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;

namespace zip_archive {

void create(const fs::path& source_directory, const fs::path& destination_zip);
void extract(const fs::path& source_zip, const fs::path& destination_directory);

}
