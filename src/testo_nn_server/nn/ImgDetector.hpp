
#pragma once

#include <vector>
#include <stb/Image.hpp>
#include "Img.hpp"

namespace nn {

struct ImgDetector {
	static ImgDetector& instance();

	ImgDetector(const ImgDetector& root) = delete;
	ImgDetector& operator=(const ImgDetector&) = delete;

	std::vector<Img> detect(const stb::Image<stb::RGB>* image, const std::string& ref_img_path, double match_threshold = 0.95);
	std::vector<Img> detect(const stb::Image<stb::RGB>* image, const stb::Image<stb::RGBA>* ref_img, double match_threshold = 0.95);

private:
	ImgDetector() = default;
};

}
