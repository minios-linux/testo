
#include "ModeRun.hpp"
#include "../IR/Program.hpp"
#include "../parser/Parser.hpp"
#include "../Utils.hpp"
#include "../Logger.hpp"

void RunModeArgs::validate() const {
	ProgramConfig::validate();
	if (!bootstrap_file.empty() && !fs::is_regular_file(bootstrap_file)) {
		throw std::runtime_error("Bootstrap file does not exist or is not a regular file: " + bootstrap_file);
	}
}

int run_mode(const RunModeArgs& args) {
	TRACE();

	args.validate();
	auto parser = Parser::load(args.target);
	auto ast = parser.parse();
	std::shared_ptr<AST::Program> bootstrap_ast;
	if (!args.bootstrap_file.empty()) {
		auto bootstrap_parser = Parser::load(args.bootstrap_file);
		bootstrap_ast = bootstrap_parser.parse();
	}
	IR::Program program(ast, args, bootstrap_ast);
	program.validate();
	program.run();

	return 0;
}
