#include "ReportWriterJUnit.hpp"
#include <pugixml/pugixml.hpp>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

std::string seconds_string(std::chrono::system_clock::duration duration) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6)
       << std::chrono::duration<double>(duration).count();
    return ss.str();
}

std::string duration_string(std::chrono::system_clock::duration duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    return std::to_string(hours.count()) + "h:" + std::to_string(minutes.count()) +
        "m:" + std::to_string(seconds.count()) + "s";
}

std::string status_string(IR::TestRun::ExecStatus status) {
    switch (status) {
        case IR::TestRun::ExecStatus::Passed: return "passed";
        case IR::TestRun::ExecStatus::Failed: return "failed";
        case IR::TestRun::ExecStatus::Skipped: return "skipped";
        default: return "unknown";
    }
}

std::string skip_message(const std::shared_ptr<IR::TestRun>& run) {
    std::vector<std::shared_ptr<IR::TestRun>> failed_parents;
    for (const auto& parent: run->parents) {
        if (parent->exec_status != IR::TestRun::ExecStatus::Passed) {
            failed_parents.push_back(parent);
        }
    }
    std::sort(failed_parents.begin(), failed_parents.end(), [](const auto& a, const auto& b) {
        return a->test->name() < b->test->name();
    });
    if (!failed_parents.empty()) {
        const auto& parent = failed_parents.front();
        return "Test " + run->test->name() + " skipped. Parent test " + parent->test->name() +
            " has status " + status_string(parent->exec_status) + ".";
    }
    if (!run->unsuccessful_deps_names.empty()) {
        return "Test " + run->test->name() + " skipped. Dependency test " +
            *run->unsuccessful_deps_names.begin() + " has failed or was skipped.";
    }
    if (run->test->has_repls) {
        return "Test " + run->test->name() + " skipped because it has a REPL action.";
    }
    return "Test " + run->test->name() + " skipped.";
}

struct JUnitCase {
    std::string name;
    std::shared_ptr<IR::Test> cached_test;
    std::shared_ptr<IR::TestRun> run;
};

} // namespace

ReportWriterJUnit::ReportWriterJUnit(const std::string& report_path_):
    ReportWriter(ReportConfig{}), report_path(report_path_) {}

void ReportWriterJUnit::launch_begin(const std::vector<std::shared_ptr<IR::Test>>& tests,
    const std::vector<std::shared_ptr<IR::TestRun>>& runs)
{
    start_timestamp = std::chrono::system_clock::now();
    tests_runs = runs;
    for (const auto& test: tests) {
        if (test->is_up_to_date()) {
            up_to_date_tests.push_back(test);
        }
    }
}

void ReportWriterJUnit::test_skip_begin(const std::shared_ptr<IR::TestRun>&) {}

void ReportWriterJUnit::report(const std::shared_ptr<IR::TestRun>& run, const std::string& text) {
    system_output += text;
    if (run) {
        test_output[run->id] += text;
    }
}

void ReportWriterJUnit::report_raw(const std::shared_ptr<IR::TestRun>& run, const std::string& text) {
    report(run, text);
}

void ReportWriterJUnit::launch_end() {
    std::vector<JUnitCase> cases;
    for (const auto& test: up_to_date_tests) {
        cases.push_back({test->name(), test, nullptr});
    }
    for (const auto& run: tests_runs) {
        cases.push_back({run->test->name(), nullptr, run});
    }
    std::stable_sort(cases.begin(), cases.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    size_t failures = 0;
    size_t skipped = up_to_date_tests.size();
    for (const auto& run: tests_runs) {
        if (run->exec_status == IR::TestRun::ExecStatus::Failed) ++failures;
        if (run->exec_status == IR::TestRun::ExecStatus::Skipped) ++skipped;
    }

    pugi::xml_document doc;
    auto declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    auto suite = doc.append_child("testsuite");
    suite.append_attribute("tests") = static_cast<unsigned long long>(cases.size());
    suite.append_attribute("failures") = static_cast<unsigned long long>(failures);
    suite.append_attribute("skipped") = static_cast<unsigned long long>(skipped);
    suite.append_attribute("errors") = 0;
    suite.append_attribute("time") = seconds_string(std::chrono::system_clock::now() - start_timestamp).c_str();

    for (const auto& item: cases) {
        auto testcase = suite.append_child("testcase");
        testcase.append_attribute("name") = item.name.c_str();
        if (item.cached_test) {
            testcase.append_attribute("time") = "0.000000";
            auto skipped_node = testcase.append_child("skipped");
            auto message = "test " + item.name + " is up-to-date";
            skipped_node.append_attribute("message") = message.c_str();
            skipped_node.text().set("This test is cached");
            continue;
        }

        const auto& run = item.run;
        std::string time = "0.000000";
        if (run->exec_status == IR::TestRun::ExecStatus::Passed || run->exec_status == IR::TestRun::ExecStatus::Failed) {
            time = seconds_string(run->duration());
        }
        testcase.append_attribute("time") = time.c_str();

        if (run->exec_status == IR::TestRun::ExecStatus::Failed) {
            auto failure = testcase.append_child("failure");
            auto message = "Test " + item.name + " FAILED in " + duration_string(run->duration());
            failure.append_attribute("message") = message.c_str();
            failure.text().set(test_output[run->id].c_str());
        } else if (run->exec_status == IR::TestRun::ExecStatus::Skipped) {
            auto skipped_node = testcase.append_child("skipped");
            auto message = skip_message(run);
            skipped_node.append_attribute("message") = message.c_str();
            skipped_node.text().set(message.c_str());
        }
    }

    auto out = suite.append_child("system-out");
    out.text().set(system_output.c_str());

    if (!doc.save_file(report_path.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
        throw std::runtime_error("Failed to write JUnit report: " + report_path);
    }
}
