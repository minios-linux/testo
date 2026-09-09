#include <catch.hpp>

#include "../IR/Action.hpp"
#include "../IR/Program.hpp"
#include "../parser/Parser.hpp"

namespace {

std::shared_ptr<AST::Exec> parse_exec(const std::string& text) {
    auto action = Parser(".", text + "\n", false).action();
    if (auto delimited = std::dynamic_pointer_cast<AST::ActionWithDelim>(action)) {
        action = delimited->action;
    }
    auto exec = std::dynamic_pointer_cast<AST::Exec>(action);
    REQUIRE(exec != nullptr);
    return exec;
}

IR::Exec make_exec(const std::string& text, const std::shared_ptr<StackNode>& stack) {
    return IR::Exec(parse_exec(text), stack, std::make_shared<VarMap>());
}

} // namespace

TEST_CASE("exec option built-in defaults") {
    ProgramConfig config;
    auto ast = Parser(".", "", false).parse();
    IR::Program program(ast, config);

    auto exec = make_exec("exec bash \"true\"", program.stack);
    REQUIRE(exec.as().empty());
    REQUIRE(exec.expect().empty());
    REQUIRE(exec.with() == "none");
}
TEST_CASE("exec option parameter defaults") {
    ProgramConfig config;
    config.params_names = {
        "TESTO_EXEC_DEFAULT_AS",
        "TESTO_EXEC_DEFAULT_EXPECT",
        "TESTO_EXEC_DEFAULT_WITH",
    };
    config.params_values = {
        "\"nobody\"",
        "\"DEFAULT_MAGIC\"",
        "systemd-run",
    };

    auto ast = Parser(".", "", false).parse();
    IR::Program program(ast, config);

    auto exec = make_exec("exec bash \"true\"", program.stack);
    REQUIRE(exec.as() == "nobody");
    REQUIRE(exec.expect() == "DEFAULT_MAGIC");
    REQUIRE(exec.with() == "systemd-run");
}

TEST_CASE("explicit exec options override parameter defaults") {
    ProgramConfig config;
    config.params_names = {
        "TESTO_EXEC_DEFAULT_AS",
        "TESTO_EXEC_DEFAULT_EXPECT",
        "TESTO_EXEC_DEFAULT_WITH",
    };
    config.params_values = {
        "\"nobody\"",
        "\"DEFAULT_MAGIC\"",
        "systemd-run",
    };

    auto ast = Parser(".", "", false).parse();
    IR::Program program(ast, config);

    auto exec = make_exec(
        "exec bash \"true\" as \"root\" expect \"EXPLICIT\" with global",
        program.stack);
    REQUIRE(exec.as() == "root");
    REQUIRE(exec.expect() == "EXPLICIT");
    REQUIRE(exec.with() == "global");
}

TEST_CASE("quoted exec with option is resolved as a string") {
    ProgramConfig config;
    auto ast = Parser(".", "", false).parse();
    IR::Program program(ast, config);

    auto exec = make_exec(
        "exec bash \"true\" as \"nobody\" with \"systemd-run\"",
        program.stack);
    REQUIRE(exec.as() == "nobody");
    REQUIRE(exec.with() == "systemd-run");
}
