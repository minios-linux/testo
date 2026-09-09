#pragma once

#include <ghc/filesystem.hpp>
#include <map>
#include <string>
#include <vector>

namespace fs = ghc::filesystem;

struct NeedleReference {
    fs::path path;
    double match = 0.0;
    std::string source_signature;
};

class Needles {
public:
    Needles() = default;
    ~Needles();

    Needles(const Needles&) = delete;
    Needles& operator=(const Needles&) = delete;

    void load(const fs::path& directory, const fs::path& allowed_sharing_directory);
    const std::vector<NeedleReference>& find(const std::string& tag) const;
    bool has(const std::string& tag) const;
    std::string signature(const std::string& tag) const;

private:
    fs::path cache_directory;
    std::map<std::string, std::vector<NeedleReference>> by_tag;
};
