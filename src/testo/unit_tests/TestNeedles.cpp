#include <catch.hpp>
#include "../Needles.hpp"
#include "../Utils.hpp"
#include <stb/Image.hpp>

#include <fstream>

namespace {
struct TempTree {
    fs::path root = fs::temp_directory_path() / ("testo-needles-unit-" + generate_uuid_v4());
    fs::path needles = root / "needles";
    fs::path share = root / "share";
    TempTree() { fs::create_directories(needles); fs::create_directories(share); }
    ~TempTree() { try { if (fs::exists(root)) fs::remove_all(root); } catch (...) {} }
};

void write_fixture(const TempTree& tree) {
    stb::Image<stb::RGB> image(4, 4, stb::RGB::black());
    image.at(0, 0) = {255, 0, 0};
    image.at(1, 0) = {0, 255, 0};
    image.at(2, 2) = {0, 0, 255};
    image.write_png((tree.needles / "sample.png").generic_string());
    std::ofstream out((tree.needles / "sample.json").generic_string());
    out << R"({
      "first": {"tags":"alpha","xpos":0,"ypos":0,"width":2,"height":1,"match":0.8},
      "second":{"tags":["alpha","beta"],"xpos":2,"ypos":2,"width":1,"height":1,"match":0.6}
    })";
}
}

TEST_CASE("load and resolve current Testo needles") {
    TempTree tree;
    write_fixture(tree);
    Needles needles;
    needles.load(tree.needles, tree.share);
    REQUIRE(needles.has("alpha"));
    REQUIRE(needles.has("beta"));
    REQUIRE_FALSE(needles.has("missing"));
    REQUIRE(needles.find("alpha").size() == 2);
    REQUIRE(needles.find("beta").size() == 1);
    REQUIRE(needles.find("alpha")[0].match == Approx(0.8));
    REQUIRE(needles.find("alpha")[1].match == Approx(0.6));
    stb::Image<stb::RGB> first(needles.find("alpha")[0].path.generic_string());
    REQUIRE(first.w == 2);
    REQUIRE(first.h == 1);
}

TEST_CASE("reloading needles removes previous generated cache") {
    TempTree tree;
    write_fixture(tree);
    Needles needles;
    needles.load(tree.needles, tree.share);
    auto old_path = needles.find("alpha").front().path;
    REQUIRE(fs::exists(old_path));
    needles.load({}, tree.share);
    REQUIRE_FALSE(fs::exists(old_path));
    REQUIRE_FALSE(needles.has("alpha"));
}
