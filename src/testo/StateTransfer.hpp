#pragma once

#include <ghc/filesystem.hpp>

namespace IR { struct Program; }
namespace fs = ghc::filesystem;

namespace state_transfer {

void export_directory(const IR::Program& program, const fs::path& destination, bool user_mode);
void import_directory(const fs::path& source, bool force, bool user_mode);

}
