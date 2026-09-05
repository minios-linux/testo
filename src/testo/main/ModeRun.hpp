
#pragma once

#include <vector>
#include <string>
#include "../Configs.hpp"

struct RunModeArgs: ProgramConfig {
	std::string export_path;
	std::string bootstrap_file;
	void validate() const;
};

int run_mode(const RunModeArgs& args);