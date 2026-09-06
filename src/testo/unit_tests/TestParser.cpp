
#include <catch.hpp>
#include <version_number/VersionNumber.hpp>
#include "../parser/Parser.hpp"

void TestParseStringifyActions(const std::string& str) {
	auto block = Parser(".", str).action_block();
	auto str2 = block->to_string();
	REQUIRE(str == str2);
}

TEST_CASE("parse current mouse wheel actions") {
	TestParseStringifyActions("{ mouse wheel-up; }");
	TestParseStringifyActions("{ mouse wheel-down scroll 3; }");
	TestParseStringifyActions("{ mouse wheel-down \"target\" timeout 2s interval 500ms scroll 4; }");
	TestParseStringifyActions("{ mouse wheel-up imgtag \"target\"; }");
}

TEST_CASE("parse STRMATCH comparison") {
	const std::string source = R"(
machine vm {
	ram: 256Mb
	cpus: 1
	disk main: {
		size: 1Gb
	}
}
test match {
	vm {
		if ("abc" STRMATCH "a.*") {
			sleep 1ms
		}
	}
}
)";
	REQUIRE_NOTHROW(Parser(".", source).parse());
}

TEST_CASE("parse step action") {
	TestParseStringifyActions("{ step; }");
}

TEST_CASE("parse current exec options") {
	TestParseStringifyActions("{ exec bash \"echo ok\" as \"live\" expect \"ok\" with systemd-run; }");
	TestParseStringifyActions("{ exec bash \"echo ok\" as \"live\" expect \"ok\" with \"systemd-run\"; }");
}

TEST_CASE("parse modern two- and three-part versions") {
	VersionNumber modern("15.0");
	REQUIRE(modern.MAJOR == 15);
	REQUIRE(modern.MINOR == 0);
	REQUIRE(modern.PATCH == 0);

	VersionNumber legacy("3.6.8");
	REQUIRE(legacy.MAJOR == 3);
	REQUIRE(legacy.MINOR == 6);
	REQUIRE(legacy.PATCH == 8);
	REQUIRE(legacy < modern);
	REQUIRE_THROWS(VersionNumber("15"));
}

TEST_CASE("parse action imgtag selectors") {
	TestParseStringifyActions("{ wait imgtag \"login-button\" timeout 2s; }");
	TestParseStringifyActions("{ mouse click imgtag \"login-button\"; }");
}

TEST_CASE("parse hotplug actions") {
	TestParseStringifyActions("{ ram add 64Mb; }");
	TestParseStringifyActions("{ ram remove 32Mb; }");
	TestParseStringifyActions("{ cpu add 1; }");
	TestParseStringifyActions("{ cpu remove 1; }");
}

TEST_CASE("parse current Testo metadata attributes") {
	const std::string source = R"(
[
	title: "Metadata test"
	description: "metadata probe"
	feature: "core"
	story: "compat"
	severity: "normal"
	epic: "modernization"
	owner: "alice"
	flaky: true
	issues: {{"ISSUE-1":"https://example.invalid/1"}}
	labels: {{"layer":"gui","speed":"fast"}}
]
test sample {
}
)";
	REQUIRE_NOTHROW(Parser(".", source).parse());
}

TEST_CASE("parse metadata raw JSON through a string") {
	const std::string source = R"(
[
	issues: "{{\"ISSUE-1\":\"https://example.invalid/1\"}}"
	labels: "{{\"layer\":\"gui\"}}"
]
test sample {
}
)";
	REQUIRE_NOTHROW(Parser(".", source).parse());
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
	setup_bootstrap_test: true
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
