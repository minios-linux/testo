
#include <catch.hpp>
#include "../parser/Parser.hpp"

void TestParseStringifyActions(const std::string& str) {
	auto block = Parser(".", str).action_block();
	auto str2 = block->to_string();
	REQUIRE(str == str2);
}

TEST_CASE("parse action imgtag selectors") {
	TestParseStringifyActions("{ wait imgtag \"login-button\" timeout 2s; }");
	TestParseStringifyActions("{ mouse click imgtag \"login-button\"; }");
}

TEST_CASE("parse action wait") {
	TestParseStringifyActions("{ wait \"hello world\" interval 32s timeout 65ms; }");
	TestParseStringifyActions("{ wait \"hello world\" timeout 65ms interval 32s; }");
	TestParseStringifyActions("{ wait \"hello world\"; }");
	TestParseStringifyActions("{ wait \"hello world\" timeout 65ms; }");
	TestParseStringifyActions("{ wait \"hello world\" interval 32s; }");
	TestParseStringifyActions("{ wait \"hello world\" timeout \"${SOME_PARAM}\" interval \"some_prefix_${SOME_OTHER_PARAM}\"; }");
}

TEST_CASE("parse action type") {
	TestParseStringifyActions("{ type \"hello world\" interval 32s; }");
	TestParseStringifyActions("{ type \"hello world\" interval 32s autoswitch LEFTALT+LEFTSHIFT; }");
	TestParseStringifyActions("{ type \"hello world\" interval \"1ms\" autoswitch \"LEFTALT+SPACE\"; }");
}

TEST_CASE("parse action macro call") {
	TestParseStringifyActions("{ some_macro(); }");
	TestParseStringifyActions("{ some_macro(\"10\", \"hello world\"); }");
}

TEST_CASE("parse action mouse click") {
	TestParseStringifyActions("{ mouse click \"Next\".from_right(0).center_bottom(); }");
}

TEST_CASE("parse modern machine resources and boot order") {
	const std::string source = R"(
machine vm {
	ram: 1Gb
	ram_max: 2Gb
	cpus: 1
	cpus_max: 2
	cpu_model: "qemu64"
	graphics: {
		spice_address: "127.0.0.1"
		spice_port: 5999
	}
	iso: {
		source: "system.iso"
		boot_order: 7
	}
	disk main: {
		size: 4Gb
		boot_order: 3
	}
	nic net: {
		attached_to: lan
		boot_order: 5
	}
	nic bridge: {
		attached_to_br: "br-test"
	}
}

network lan {
	mode: "nat"
}

test smoke {
}
)";
	REQUIRE_NOTHROW(Parser(".", source).parse());
}
