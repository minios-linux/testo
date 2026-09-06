#include "ReportWriterAllure.hpp"
#include "../IR/Program.hpp"
#include <chrono>

static int64_t allure_milliseconds(std::chrono::system_clock::time_point tp) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

AllureAttachment::AllureAttachment(const fs::path& report_folder, const std::string& str) {
	title = "Output";
	type = "text/plain";
	source = generate_uuid_v4() + "-attachment.txt";
	std::ofstream file(report_folder / source);
	file << str;
}

AllureAttachment::AllureAttachment(const fs::path& report_folder, const stb::Image<stb::RGB>& screenshot, const std::string& tag) {
	title = "Screenshot " + tag;
	type = "image/png";
	source = generate_uuid_v4() + "-attachment.png";
	screenshot.write_png((report_folder / source).generic_string());
}

nlohmann::json AllureAttachment::to_json() const {
	return {{"source", source}, {"title", title}, {"type", type}};
}

void AllureStep::finish(const std::string& status_) {
	status = status_;
	stop = std::chrono::system_clock::now();
}

nlohmann::json AllureStep::to_json() const {
	nlohmann::json j = {
		{"attachments", nlohmann::json::array()},
		{"name", name},
		{"start", allure_milliseconds(start)},
		{"status", status},
		{"stop", allure_milliseconds(stop)},
	};
	for (const auto& attachment: attachments) {
		j["attachments"].push_back(attachment.to_json());
	}
	return j;
}

static void allure_add_label(nlohmann::json& labels, const std::string& name, const std::string& value) {
	labels.push_back({{"name", name}, {"value", value}});
}

AllureTestCase::AllureTestCase(const std::shared_ptr<IR::Test>& test) {
	name = test->name();
	// Current Testo uses the declaration name as the Allure title.
	title = name;
	description = test->description();
	flaky = test->attrs().value("flaky", false);

	auto suite = test->get_source_file_path().parent_path().filename().generic_string();
	if (suite.empty()) suite = ".";
	allure_add_label(labels, "suite", suite);

	for (const char* attr: {"feature", "story", "severity", "epic", "owner"}) {
		if (test->attrs().count(attr)) {
			allure_add_label(labels, attr, test->attrs().at(attr).get<std::string>());
		}
	}

	if (test->attrs().count("labels")) {
		auto custom = nlohmann::json::parse(test->attrs().at("labels").get<std::string>());
		for (auto it = custom.begin(); it != custom.end(); ++it) {
			allure_add_label(labels, it.key(), it.value().get<std::string>());
		}
	}

	if (test->attrs().count("issues")) {
		auto issues = nlohmann::json::parse(test->attrs().at("issues").get<std::string>());
		for (auto it = issues.begin(); it != issues.end(); ++it) {
			links.push_back({{"name", it.key()}, {"type", "issue"}, {"url", it.value().get<std::string>()}});
		}
	}
}

nlohmann::json AllureTestCase::to_json() const {
	nlohmann::json j = {
		{"description", description},
		{"flaky", flaky},
		{"labels", labels},
		{"links", links},
		{"name", name},
		{"start", allure_milliseconds(start)},
		{"status", status},
		{"stop", allure_milliseconds(stop)},
		{"title", title},
		{"uuid", uuid},
	};
	if (!steps.empty()) {
		j["steps"] = nlohmann::json::array();
		for (const auto& step: steps) j["steps"].push_back(step.to_json());
	}
	return j;
}

ReportWriterAllure::ReportWriterAllure(const ReportConfig& config): ReportWriter(config) {
	report_folder = config.report_folder;
}

void ReportWriterAllure::write_result(const AllureTestCase& testcase) {
	std::ofstream file(report_folder / (testcase.uuid + "-result.json"));
	file << testcase.to_json().dump();
}

void ReportWriterAllure::launch_begin(const std::vector<std::shared_ptr<IR::Test>>& tests,
	const std::vector<std::shared_ptr<IR::TestRun>>& tests_runs)
{
	fs::create_directories(report_folder);
	write_environment_file();
	write_categories_file();

	for (const auto& test: tests) {
		if (test->is_up_to_date()) {
			AllureTestCase testcase(test);
			testcase.status = "unknown";
			testcase.start = std::chrono::system_clock::now();
			testcase.stop = testcase.start;
			write_result(testcase);
		}
	}
}

void ReportWriterAllure::test_skip_begin(const std::shared_ptr<IR::TestRun>& test_run) {
	test_begin(test_run);
}

void ReportWriterAllure::test_skip_end(const std::shared_ptr<IR::TestRun>& test_run) {
	finish_last_step("skipped");
	current_testcase.stop = std::chrono::system_clock::now();
	current_testcase.status = "skipped";
	write_result(current_testcase);
}

void ReportWriterAllure::test_begin(const std::shared_ptr<IR::TestRun>& test_run) {
	current_testcase = AllureTestCase(test_run->test);
	current_testcase.start = std::chrono::system_clock::now();
}

void ReportWriterAllure::finish_last_step(const std::string& status) {
	if (current_testcase.steps.empty()) return;
	auto& step = current_testcase.steps.back();
	step.finish(status);
	if (!step.raw.empty()) {
		step.attachments.emplace_back(report_folder, step.raw);
		step.raw.clear();
	}
}

void ReportWriterAllure::report_prefix(const std::shared_ptr<IR::TestRun>& test_run) {
	finish_last_step("passed");
	AllureStep step;
	step.start = std::chrono::system_clock::now();
	current_testcase.steps.push_back(std::move(step));
}

void ReportWriterAllure::report(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) {
	if (!current_testcase.steps.empty()) current_testcase.steps.back().name += text;
}

void ReportWriterAllure::report_raw(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) {
	if (!current_testcase.steps.empty()) current_testcase.steps.back().raw += text;
}

void ReportWriterAllure::report_screenshot(const std::shared_ptr<IR::TestRun>& test_run,
	const stb::Image<stb::RGB>& screenshot, const std::string& tag)
{
	if (!current_testcase.steps.empty()) {
		current_testcase.steps.back().attachments.emplace_back(report_folder, screenshot, tag);
	}
}

void ReportWriterAllure::test_end(const std::shared_ptr<IR::TestRun>& test_run) {
	std::string step_status = "passed";
	switch (test_run->exec_status) {
		case IR::TestRun::ExecStatus::Passed:
			current_testcase.status = "passed";
			break;
		case IR::TestRun::ExecStatus::Failed:
			current_testcase.status = "failed";
			step_status = "failed";
			break;
		case IR::TestRun::ExecStatus::Skipped:
			current_testcase.status = "skipped";
			step_status = "skipped";
			break;
		default:
			current_testcase.status = "broken";
			step_status = "broken";
			break;
	}
	finish_last_step(step_status);
	current_testcase.stop = std::chrono::system_clock::now();
	write_result(current_testcase);
}

void ReportWriterAllure::launch_end() {
}

void ReportWriterAllure::write_environment_file() {
	std::ofstream file(report_folder / "environment.properties");
	for (const auto& kv: IR::program->stack->params) file << kv.first << "=" << kv.second << std::endl;
}

void ReportWriterAllure::write_categories_file() {
	nlohmann::json j = {
		{{"name", "Passed tests"}, {"matchedStatuses", {"passed"}}},
		{{"name", "Failed tests"}, {"matchedStatuses", {"failed"}}},
		{{"name", "Skipped tests"}, {"matchedStatuses", {"skipped"}}},
		{{"name", "Up-to-date tests"}, {"matchedStatuses", {"unknown"}}},
	};
	std::ofstream file(report_folder / "categories.json");
	file << j.dump(2);
}
