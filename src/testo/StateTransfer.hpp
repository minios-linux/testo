#pragma once

#include <ghc/filesystem.hpp>
#include <memory>

namespace IR { struct Program; struct Test; }
namespace fs = ghc::filesystem;

namespace state_transfer {

void export_state(const IR::Program& program, const fs::path& destination, bool user_mode);
void export_test_state(const std::shared_ptr<IR::Test>& test, const fs::path& destination, bool user_mode, bool replace_destination);
void import_state(const fs::path& source, bool force, bool user_mode);

}
