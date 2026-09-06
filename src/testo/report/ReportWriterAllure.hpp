#pragma once

#include "ReportWriter.hpp"

struct AllureAttachment {
	AllureAttachment(const fs::path& report_folder, const std::string& str);
	AllureAttachment(const fs::path& report_folder, const stb::Image<stb::RGB>& screenshot, const std::string& tag);

	nlohmann::json to_json() const;

	std::string title;
	std::string source;
	std::string type;
};

struct AllureStep {
	nlohmann::json to_json() const;
	void finish(const std::string& status_);

	std::chrono::system_clock::time_point start;
	std::chrono::system_clock::time_point stop;
	std::string name;
	std::string status;
	std::vector<AllureAttachment> attachments;
	std::string raw;
};

struct AllureTestCase {
	AllureTestCase() = default;
	AllureTestCase(const std::shared_ptr<IR::Test>& test);

	nlohmann::json to_json() const;

	std::string uuid = generate_uuid_v4();
	std::string name;
	std::string title;
	std::string description;
	bool flaky = false;
	std::string status;
	std::chrono::system_clock::time_point start;
	std::chrono::system_clock::time_point stop;
	nlohmann::json labels = nlohmann::json::array();
	nlohmann::json links = nlohmann::json::array();
	std::vector<AllureStep> steps;
};

struct ReportWriterAllure: ReportWriter {
	ReportWriterAllure(const ReportConfig& config);

	void launch_begin(const std::vector<std::shared_ptr<IR::Test>>& tests,
		const std::vector<std::shared_ptr<IR::TestRun>>& tests_runs) override;
	void test_skip_begin(const std::shared_ptr<IR::TestRun>& test_run) override;
	void test_skip_end(const std::shared_ptr<IR::TestRun>& test_run) override;
	void test_begin(const std::shared_ptr<IR::TestRun>& test_run) override;
	void report_prefix(const std::shared_ptr<IR::TestRun>& test_run) override;
	void report(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) override;
	void report_raw(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) override;
	void report_screenshot(const std::shared_ptr<IR::TestRun>& test_run, const stb::Image<stb::RGB>& screenshot, const std::string& tag) override;
	void test_end(const std::shared_ptr<IR::TestRun>& test_run) override;
	void launch_end() override;

private:
	void write_environment_file();
	void write_categories_file();
	void write_result(const AllureTestCase& testcase);
	void finish_last_step(const std::string& status);

	AllureTestCase current_testcase;
	fs::path report_folder;
};
