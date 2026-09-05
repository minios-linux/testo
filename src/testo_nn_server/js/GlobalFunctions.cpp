
#include "GlobalFunctions.hpp"
#include "Tensor.hpp"
#include <iostream>

namespace js {

Value print(ContextRef ctx, const ValueRef this_val, const std::vector<ValueRef>& args) {
	auto& output = ctx.get_stdout();
	for (size_t i = 0; i < args.size(); i++) {
		if (i != 0) {
			std::cout << ' ';
		}
		output << args[i];
	}
	output << std::endl;
	return ctx.new_undefined();
}


Value find_text(ContextRef ctx, const ValueRef this_val, const std::vector<ValueRef>& args) {
	if (args.size() > 1) {
		throw std::runtime_error("Invalid arguments count in find_text");
	}

	nn::TextTensor tensor = nn::find_text(ctx.image());
	if (args.size() == 1) {
		std::string text = std::string(args.at(0));
		tensor = tensor.match_text(ctx.image(), text);
	}
	
	return TextTensor(ctx, tensor);
}

Value find_img(ContextRef ctx, const ValueRef this_val, const std::vector<ValueRef>& args) {
	if (args.size() != 1) {
		throw std::runtime_error("Invalid arguments count in find_img");
	}

	std::string img_path = args.at(0);

	stb::Image<stb::RGBA> ref_image = ctx.env()->get_ref_image(img_path);

	double match_threshold = 0.95;
	auto global = ctx.get_global_object();
	auto threshold = global.get_property_str("image_match_threshold");
	if (!threshold.is_undefined()) {
		if (JS_ToFloat64(ctx.handle, &match_threshold, threshold.handle) != 0) {
			throw std::runtime_error("image_match_threshold must be a number");
		}
	}

	nn::ImgTensor tensor = nn::find_img(ctx.image(), &ref_image, match_threshold);
	return ImgTensor(ctx, tensor);
}

}
