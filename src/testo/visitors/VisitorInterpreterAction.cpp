
#include <coro/Timer.h>
#include "VisitorInterpreterAction.hpp"
#include "ReplState.hpp"
#include "../Exceptions.hpp"
#include "../IR/Program.hpp"
#include "../IR/Test.hpp"
#include <coro/Finally.h>
#include <cmath>
#include "../Logger.hpp"

static nlohmann::json snapshot_stack_frames(const std::shared_ptr<StackNode>& stack) {
	nlohmann::json result = nlohmann::json::array();
	for (auto frame = stack; frame; frame = frame->parent) {
		result.push_back({{"params", frame->params}});
	}
	return result;
}

bool SnapshotResumeContext::matches(const Pos& pos, const std::shared_ptr<StackNode>& stack) const {
	if (pos.file != file || pos.offset != offset) {
		return false;
	}
	return stack_frames.empty() || snapshot_stack_frames(stack) == stack_frames;
}

void VisitorInterpreterAction::visit_action_block(std::shared_ptr<AST::Block<AST::Action>> action_block) {
	for (auto action: action_block->items) {
		visit_action(action);
	}
}

bool VisitorInterpreterAction::handle_fast_forward(const std::shared_ptr<AST::Action>& action) {
	if (!resume_context || !resume_context->active) {
		return false;
	}
	if (auto p = std::dynamic_pointer_cast<AST::ActionWithDelim>(action)) {
		visit_action(p->action);
		return true;
	}
	if (auto p = std::dynamic_pointer_cast<AST::SnapshotCreate>(action)) {
		if (resume_context->matches(p->begin(), stack)) {
			resume_context->active = false;
			resume_context->target_reached = true;
		}
		return true;
	}
	if (auto p = std::dynamic_pointer_cast<AST::Block<AST::Action>>(action)) {
		// Fast-forward scans only the direct actions of the controller's root
		// block. A checkpoint hidden inside a nested block, macro, if, or for is
		// deliberately not reachable during fast-forward.
		if (resume_context->scanning_root_block) {
			return true;
		}
		resume_context->scanning_root_block = true;
		visit_action_block(p);
		resume_context->scanning_root_block = false;
		return true;
	}
	if (std::dynamic_pointer_cast<AST::MacroCall<AST::Action>>(action) ||
		std::dynamic_pointer_cast<AST::IfClause>(action) ||
		std::dynamic_pointer_cast<AST::ForClause>(action) ||
		std::dynamic_pointer_cast<AST::CycleControl>(action)) {
		return true;
	}
	return true;
}

void VisitorInterpreterAction::before_action(const std::shared_ptr<AST::Action>& action) {
	const bool atomic =
		!std::dynamic_pointer_cast<AST::ActionWithDelim>(action) &&
		!std::dynamic_pointer_cast<AST::MacroCall<AST::Action>>(action) &&
		!std::dynamic_pointer_cast<AST::IfClause>(action) &&
		!std::dynamic_pointer_cast<AST::ForClause>(action) &&
		!std::dynamic_pointer_cast<AST::CycleControl>(action) &&
		!std::dynamic_pointer_cast<AST::Block<AST::Action>>(action) &&
		!std::dynamic_pointer_cast<AST::Empty>(action);
	if (!atomic) {
		return;
	}

	if (atomic_action_seen) {
		const auto value = IR::program->resolve_top_level_param("TESTO_ACTION_WAIT_INTERVAL");
		if (!value.empty()) {
			coro::Timer timer;
			timer.waitFor(IR::time_to_milliseconds(value));
		}
	}
	atomic_action_seen = true;
}

std::chrono::milliseconds VisitorInterpreterAction::scaled_action_timeout(std::chrono::milliseconds timeout) const {
	const double coeff = std::stod(IR::program->resolve_top_level_param("TESTO_TIMEOUT_COEFF"));
	if (!std::isfinite(coeff)) {
		// nan/inf pass validation and turn the
		// resulting action timeout into an immediate deadline.
		return std::chrono::milliseconds(0);
	}
	const long double scaled = static_cast<long double>(timeout.count()) * coeff;
	if (scaled >= static_cast<long double>(std::chrono::milliseconds::max().count())) {
		return std::chrono::milliseconds::max();
	}
	return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(scaled));
}

void VisitorInterpreterAction::debug_pause() {
	if (!debug) {
		return;
	}

	std::cout << "> (please press enter to continue)";
	std::string line;
	std::getline(std::cin, line);
	if (std::cin.fail() || std::cin.eof()) {
		std::cin.clear();
	}
}

void VisitorInterpreterAction::visit_print(const IR::Print& print) {
	TRACE();
	try {
		reporter.print(current_controller, print);
	} catch (const std::exception& error) {
		std::throw_with_nested(ActionException(print.ast_node, current_controller));
	}
}

void VisitorInterpreterAction::visit_step(const IR::Step&) {
	TRACE();
	reporter.step();
}

static std::string snapshot_tmp_name(const std::shared_ptr<IR::Test>& test) {
	return test->name() + "_tmp";
}

static nlohmann::json snapshot_resume_metadata(
	const std::shared_ptr<IR::Test>& test,
	const Pos& pos,
	const std::shared_ptr<StackNode>& stack)
{
	nlohmann::json vm_running = nlohmann::json::object();
	for (const auto& machine: test->get_all_machines()) {
		vm_running[machine->vm()->id()] = machine->vm()->state() == VmState::Running;
	}
	return {
		{"pos", {
			{"file", pos.file.generic_string()},
			{"line", pos.line},
			{"column", pos.column},
			{"offset", pos.offset},
		}},
		{"stack_frames", snapshot_stack_frames(stack)},
		{"vm_running", vm_running},
	};
}

void VisitorInterpreterAction::visit_snapshot_create(const IR::SnapshotCreate& snapshot) {
	TRACE();
	if (!current_test) {
		throw std::runtime_error("snapshot create called outside a test context");
	}
	reporter.snapshot_create(current_controller);
	const std::string tmp = snapshot_tmp_name(current_test);
	const auto resume = snapshot_resume_metadata(current_test, snapshot.ast_node->begin(), stack);

	auto create_checkpoint = [&](const std::shared_ptr<IR::Controller>& controller) {
		if (controller->has_snapshot(tmp)) {
			auto metadata = controller->get_snapshot_metadata(tmp);
			std::string parent = metadata.value("parent", std::string());
			controller->delete_snapshot_with_children(tmp);
			controller->current_state = parent == tmp ? std::string() : parent;
		}
		controller->create_snapshot(tmp, current_test->cksum, true);
		controller->current_state = tmp;
		controller->set_snapshot_metadata(tmp, "resume", resume);
	};

	for (const auto& machine: current_test->get_all_machines()) create_checkpoint(machine);
	for (const auto& flash: current_test->get_all_flash_drives()) create_checkpoint(flash);
}

static std::string vm_state_name(VmState state) {
	switch (state) {
		case VmState::Stopped: return "Stopped";
		case VmState::Running: return "Running";
		case VmState::Suspended: return "Suspended";
		default: return "Other";
	}
}

void VisitorInterpreterAction::visit_snapshot_revert(const IR::SnapshotRevert&) {
	TRACE();
	if (!current_test) {
		throw std::runtime_error("snapshot revert called outside a test context");
	}
	reporter.snapshot_revert(current_controller);
	const std::string tmp = snapshot_tmp_name(current_test);
	const auto controllers = current_test->get_all_controllers();
	for (const auto& controller: controllers) {
		if (!controller->has_snapshot(tmp, true)) {
			throw std::runtime_error("snapshot revert: no _tmp snapshot for " + controller->type() + " " +
				controller->name() + "; snapshot create must have been called first");
		}
	}

	nlohmann::json resume;
	if (!controllers.empty()) {
		auto metadata = (*controllers.begin())->get_snapshot_metadata(tmp);
		if (metadata.count("resume")) resume = metadata.at("resume");
	}
	for (const auto& machine: current_test->get_all_machines()) machine->restore_snapshot(tmp);
	for (const auto& flash: current_test->get_all_flash_drives()) flash->restore_snapshot(tmp);

	if (resume.is_object() && resume.count("vm_running")) {
		const auto& running = resume.at("vm_running");
		for (const auto& machine: current_test->get_all_machines()) {
			const std::string id = machine->vm()->id();
			if (running.value(id, false) && machine->vm()->state() != VmState::Running) {
				throw std::runtime_error("After restoring snapshot, VM '" + id + "' is in state '" +
					vm_state_name(machine->vm()->state()) +
					"' but the recorded state at snapshot create time was Running. The snapshot most likely did not capture the VM's memory.");
			}
		}
	}
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
	ltrim(s);
	rtrim(s);
}


void VisitorInterpreterAction::visit_repl(const IR::REPL& repl) {
	TRACE();
	if (ignore_repl) {
		return;
	}
	try {
		reporter.repl_begin(current_controller, repl);
		REPL_mode_is_active = true;
		std::cout << "Now you can type commands line-by-line. Use \\ at the end of a line to continue on the next line. Use Ctrl-C to exit REPL mode." << std::endl;
		std::string all_lines;
		while (true) {
			std::cout << "> ";
			std::string line;
			std::getline(std::cin, line);
			if (std::cin.fail() || std::cin.eof()) {
				std::cin.clear();
				break;
			}
			while (!line.empty() && line.back() == '\\') {
				line.pop_back();
				std::string continuation;
				std::cout << "> ";
				std::getline(std::cin, continuation);
				if (std::cin.fail() || std::cin.eof()) {
					std::cin.clear();
					break;
				}
				line += "\n" + continuation;
			}
			trim(line);
			if (!line.size()) {
				continue;
			}
			line += "\n";
			try {
				std::shared_ptr<AST::Action> ast_action = Parser(".", line, false).action();
				all_lines += ast_action->to_string() + "\n";
				visit_action(ast_action);
			}
			catch (const AbortException&) {
				throw;
			}
			catch (const std::exception& error) {
				std::stringstream ss;
				ss << error << std::endl;
				reporter.error(ss.str());
			}
		}
		std::cout << std::endl;
		if (all_lines.size()) {
			std::cout << "You have entered the following commands:" << std::endl;
			std::cout << all_lines;
		}
		reporter.repl_end(current_controller, repl);
	} catch (const std::exception& error) {
		std::throw_with_nested(ActionException(repl.ast_node, current_controller));
	}
}

void VisitorInterpreterAction::visit_abort(const IR::Abort& abort) {
	TRACE();
	reporter.abort(current_controller, abort);
	throw AbortException(abort.ast_node, current_controller, abort.message());
}

void VisitorInterpreterAction::visit_bug(const IR::Bug& bug) {
	TRACE();
	reporter.bug(current_controller, bug);
}

void VisitorInterpreterAction::visit_sleep(const IR::Sleep& sleep) {
	TRACE();
	reporter.sleep(current_controller, sleep);
	coro::Timer timer;
	timer.waitFor(sleep.timeout().value());
}

void VisitorInterpreterAction::visit_macro_call(const IR::MacroCall& macro_call) {
	TRACE();
	reporter.macro_action_call(current_controller, macro_call);
	macro_call.visit_interpreter<AST::Action>(this);
}

void VisitorInterpreterAction::visit_macro_body(const std::shared_ptr<AST::Block<AST::Action>>& macro_body) {
	TRACE();
	visit_action_block(macro_body);
}

void VisitorInterpreterAction::visit_if_clause(std::shared_ptr<AST::IfClause> if_clause) {
	TRACE();
	bool expr_result;
	try {
		expr_result = visit_expr(if_clause->expr);
	} catch (const std::exception& error) {
		std::throw_with_nested(ActionException(if_clause, current_controller));
	}
	//everything else should be caught at test level
	if (expr_result) {
		return visit_action(if_clause->if_action);
	} else if (if_clause->has_else()) {
		return visit_action(if_clause->else_action);
	}
}

void VisitorInterpreterAction::visit_for_clause(std::shared_ptr<AST::ForClause> for_clause) {
	TRACE();

	uint32_t i = 0;

	std::vector<std::string> values;

	if (auto p = std::dynamic_pointer_cast<AST::Range>(for_clause->counter_list)) {
		values = IR::Range({p, stack}).values();
	} else {
		throw std::runtime_error("Unknown counter list type");
	}

	std::map<std::string, std::string> params;
	for (i = 0; i < values.size(); ++i) {
		params[for_clause->counter.value()] = values[i];

		try {
			auto new_stack = std::make_shared<StackNode>();
			new_stack->parent = stack;
			new_stack->params = params;
			StackPusher<VisitorInterpreterAction> new_ctx(this, new_stack);
				visit_action(for_clause->cycle_body);

		} catch (const CycleControlException& cycle_control) {
			if (cycle_control.token.type() == Token::category::break_) {
				break;
			} else if (cycle_control.token.type() == Token::category::continue_) {
				continue;
			} else {
				throw std::runtime_error("Unknown cycle control command: " + cycle_control.token.value());
			}
		}
	}

	if ((i == values.size()) && for_clause->else_token) {
		visit_action(for_clause->else_action);
	}
}

bool VisitorInterpreterAction::visit_expr(std::shared_ptr<AST::Expr> expr) {
	if (auto p = std::dynamic_pointer_cast<AST::BinOp>(expr)) {
		return visit_binop(p);
	} else if (auto p = std::dynamic_pointer_cast<AST::StringExpr>(expr)) {
		std::shared_ptr<IR::Machine> vmc = std::dynamic_pointer_cast<IR::Machine>(current_controller);
		return visit_string_expr({ p->str, stack, vmc ? vmc->get_vars() : nullptr });
	} else if (auto p = std::dynamic_pointer_cast<AST::Negation>(expr)) {
		return !visit_expr(p->expr);
	} else if (auto p = std::dynamic_pointer_cast<AST::Comparison>(expr)) {
		std::shared_ptr<IR::Machine> vmc = std::dynamic_pointer_cast<IR::Machine>(current_controller);
		return visit_comparison({ p, stack, vmc ? vmc->get_vars() : nullptr });
	} else if (auto p = std::dynamic_pointer_cast<AST::Defined>(expr)) {
		return visit_defined({ p, stack });
	} else if (auto p = std::dynamic_pointer_cast<AST::Check>(expr)) {
		std::shared_ptr<IR::Machine> vmc = std::dynamic_pointer_cast<IR::Machine>(current_controller);
		if (!vmc) {
			throw std::runtime_error("\"check\" expression is only available for VMs");
		}
		return visit_check({ p, stack, vmc->get_vars() });
	} else if (auto p = std::dynamic_pointer_cast<AST::ParentedExpr>(expr)) {
		return visit_expr(p->expr);
	} else {
		throw std::runtime_error("Unknown expr type");
	}
}

bool VisitorInterpreterAction::visit_binop(std::shared_ptr<AST::BinOp> binop) {
	auto left = visit_expr(binop->left);

	if (binop->op.value() == "AND") {
		if (!left) {
			return left;
		} else {
			return visit_expr(binop->right);
		}
	} else if (binop->op.value() == "OR") {
		if (left) {
			return left;
		} else {
			return visit_expr(binop->right);
		}
	} else {
		throw std::runtime_error("Unknown binop operation");
	}
}

bool VisitorInterpreterAction::visit_string_expr(const IR::String& string_expr) {
	return string_expr.text().length();
}

bool VisitorInterpreterAction::visit_comparison(const IR::Comparison& comparison) {
	return comparison.calculate();
}

bool VisitorInterpreterAction::visit_defined(const IR::Defined& defined) {
	return defined.is_defined();
}
