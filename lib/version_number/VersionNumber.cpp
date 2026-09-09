
#include "VersionNumber.hpp"
#include <regex>

VersionNumber::VersionNumber(const std::string& str) {
	static const std::regex regex(R"((\d+)\.(\d+)(?:\.(\d+))?)");
	std::smatch match;
	if (!std::regex_match(str, match, regex)) {
		throw std::runtime_error(__PRETTY_FUNCTION__);
	}
	MAJOR = stoi(match[1]);
	MINOR = stoi(match[2]);
	PATCH = match[3].matched ? stoi(match[3]) : 0;
}

bool VersionNumber::operator<(const VersionNumber& other) const {
	if (MAJOR < other.MAJOR) {
		return true;
	}else if (MAJOR == other.MAJOR) {
		if (MINOR < other.MINOR) {
			return true;
		} else if (MINOR == other.MINOR) {
			return PATCH < other.PATCH;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

std::string VersionNumber::to_string() const {
	return std::to_string(MAJOR) + "." + std::to_string(MINOR) + "." + std::to_string(PATCH);
}
