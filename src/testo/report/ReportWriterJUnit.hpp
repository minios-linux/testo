#pragma once

#include "ReportWriter.hpp"
#include <chrono>
#include <map>
#include <string>
#include <vector>

struct ReportWriterJUnit: ReportWriter {
    explicit ReportWriterJUnit(const std::string& report_path);

    void launch_begin(const std::vector<std::shared_ptr<IR::Test>>& tests,
        const std::vector<std::shared_ptr<IR::TestRun>>& tests_runs) override;
    void test_skip_begin(const std::shared_ptr<IR::TestRun>& test_run) override;
    void report(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) override;
    void report_raw(const std::shared_ptr<IR::TestRun>& test_run, const std::string& text) override;
    void launch_end() override;

private:
    std::string report_path;
    std::chrono::system_clock::time_point start_timestamp;
    std::vector<std::shared_ptr<IR::Test>> up_to_date_tests;
    std::vector<std::shared_ptr<IR::TestRun>> tests_runs;
    std::map<std::string, std::string> test_output;
    std::string system_output;
};
