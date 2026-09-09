#include "Needles.hpp"

#include "Utils.hpp"
#include <nlohmann/json.hpp>
#include <stb/Image.hpp>

#include <fstream>
#include <set>
#include <stdexcept>

namespace {
using json = nlohmann::json;

json read_json(const fs::path& path) {
    std::ifstream input(path.generic_string());
    if (!input) throw std::runtime_error("Can't open needle metadata: " + path.generic_string());
    json result;
    input >> result;
    return result;
}

std::vector<std::string> get_tags(const json& region) {
    std::vector<std::string> result;
    const auto& tags = region.at("tags");
    if (tags.is_string()) {
        result.push_back(tags.get<std::string>());
    } else if (tags.is_array()) {
        for (const auto& tag: tags) result.push_back(tag.get<std::string>());
    } else {
        throw std::runtime_error("Needle region 'tags' must be a string or an array of strings");
    }
    return result;
}
}

Needles::~Needles() {
    try {
        if (!cache_directory.empty() && fs::exists(cache_directory)) fs::remove_all(cache_directory);
    } catch (...) {
    }
}

void Needles::load(const fs::path& directory, const fs::path& allowed_sharing_directory) {
    by_tag.clear();
    if (!cache_directory.empty() && fs::exists(cache_directory)) {
        fs::remove_all(cache_directory);
    }
    cache_directory.clear();
    if (directory.empty()) return;
    if (!fs::is_directory(directory)) {
        throw std::runtime_error("Path for --needles " + directory.generic_string() + " is not a directory");
    }
    if (!fs::is_directory(allowed_sharing_directory)) {
        throw std::runtime_error("Allowed sharing directory is not a directory: " + allowed_sharing_directory.generic_string());
    }

    cache_directory = allowed_sharing_directory / (".testo-needles-" + generate_uuid_v4());
    if (!fs::create_directories(cache_directory)) {
        throw std::runtime_error("Can't create needle cache directory: " + cache_directory.generic_string());
    }

    size_t generated_index = 0;
    for (const auto& entry: fs::directory_iterator(directory)) {
        if (!fs::is_regular_file(entry.path()) || entry.path().extension() != ".json") continue;

        auto png_path = entry.path();
        png_path.replace_extension(".png");
        if (!fs::is_regular_file(png_path)) {
            throw std::runtime_error("Needle PNG is missing for " + entry.path().generic_string());
        }

        auto metadata = read_json(entry.path());
        if (!metadata.is_object() && !metadata.is_array()) {
            throw std::runtime_error("Needle metadata root must be an object or array: " + entry.path().generic_string());
        }
        stb::Image<stb::RGB> image(png_path.generic_string());
        const std::string source_signature = file_signature(png_path) + ";" + file_signature(entry.path());

        for (const auto& region: metadata) {
            if (!region.is_object()) continue;
            const double match = region.at("match").get<double>();
            const int xpos = region.at("xpos").get<int>();
            const int ypos = region.at("ypos").get<int>();
            const int width = region.at("width").get<int>();
            const int height = region.at("height").get<int>();
            auto tags = get_tags(region);

            if (width <= 0 || height <= 0) throw std::runtime_error("Needle region width and height must be positive");
            auto crop = image.sub_image(xpos, ypos, width, height);
            auto generated = cache_directory / (std::to_string(generated_index++) + ".png");
            crop.write_png(generated.generic_string());

            NeedleReference reference{generated, match, source_signature};
            for (const auto& tag: tags) by_tag[tag].push_back(reference);
        }
    }
}

const std::vector<NeedleReference>& Needles::find(const std::string& tag) const {
    static const std::vector<NeedleReference> empty;
    auto it = by_tag.find(tag);
    return it == by_tag.end() ? empty : it->second;
}

bool Needles::has(const std::string& tag) const {
    return !find(tag).empty();
}

std::string Needles::signature(const std::string& tag) const {
    std::string result;
    for (const auto& ref: find(tag)) result += ref.source_signature + ";" + std::to_string(ref.match) + ";";
    return result;
}
