
#pragma once

#include <string>
#include <vector>

struct CleanModeArgs {
	std::string prefix;
	std::vector<std::string> items;
	bool assume_yes = false;
	bool user_mode = false;
};

int clean_mode(const CleanModeArgs& args);
