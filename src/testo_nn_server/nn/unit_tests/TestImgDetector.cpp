#include <catch.hpp>
#include "../ImgDetector.hpp"

TEST_CASE("image match threshold controls tolerated pixel differences") {
    stb::Image<stb::RGB> search(2, 2, stb::RGB::black());
    stb::Image<stb::RGBA> reference(2, 2, stb::RGBA{0, 0, 0, 255});
    reference.at(0, 0) = {255, 255, 255, 255};

    auto loose = nn::ImgDetector::instance().detect(&search, &reference, 0.70);
    auto strict = nn::ImgDetector::instance().detect(&search, &reference, 0.80);

    REQUIRE(loose.size() == 1);
    REQUIRE(strict.empty());
}
