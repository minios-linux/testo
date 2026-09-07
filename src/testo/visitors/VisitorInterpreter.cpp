
#include <coro/CheckPoint.h>
#include <coro/AsioTask.h>
#include <coro/Finally.h>
#include <coro/CoroPool.h>
#include <coro/Timer.h>
#include "VisitorInterpreter.hpp"
#include "VisitorInterpreterActionMachine.hpp"
#include "VisitorInterpreterActionFlashDrive.hpp"
#include "VisitorSemantic.hpp"
#include "../IR/Program.hpp"
#include "../Exceptions.hpp"
#include "../Logger.hpp"
#include "../parser/Parser.hpp"
#include "../StateTransfer.hpp"
#include "../ScreenRecorder.hpp"

#include <fmt/format.h>
#include <sstream>
#include <wildcards.hpp>

VisitorInterpreter::VisitorInterpreter(const VisitorInterpreterConfig& config_): config(config_), reporter(config_) {
	TRACE();

	stop_on_fail = config_.stop_on_fail;
	repl_on_fail = config_.repl_on_fail;
	debug = config_.debug;
	assume_yes = config_.assume_yes;
	invalidate = config_.invalidate;
	dry = config_.dry;
	ignore_repl = config_.ignore_repl;
	skip_tests_with_repl = config_.skip_tests_with_repl;
	record_tests = config_.record_tests;
	repeat_failed = config_.repeat_failed;
	export_on_fail = config_.export_on_fail;
	run_as_user = config_.run_as_user;
}

VisitorInterpreter::~VisitorInterpreter() {
	TRACE();
}

void VisitorInterpreter::delete_snapshot_with_children(const std::shared_ptr<IR::Test>& test) {
	for (auto& controller: test->get_all_controllers()) {
		if (controller->is_defined() && controller->has_snapshot(test->name())) {
			controller->delete_snapshot_with_children(test->name());
		}
	}
}

void VisitorInterpreter::invalidate_tests() {
	TRACE();

	if (!invalidate.length()) {
		return;
	}
	for (auto& test: IR::program->all_selected_tests) {
		if (test->name().at(0) == '_') {
			continue;
		}
		if (wildcards::match(test->name(), invalidate)) {
			delete_snapshot_with_children(test);
		}
	}
}

void VisitorInterpreter::check_cache_missed_tests() {
	TRACE();

	if (assume_yes) {
		return;
	}

	std::vector<std::shared_ptr<IR::Test>> cache_missed_tests;

	for (auto& test: IR::program->all_selected_tests) {
		if (test->cache_status() == IR::Test::CacheStatus::Miss) {
			cache_missed_tests.push_back(test);
		}
	}

	if (!cache_missed_tests.size()) {
		return;
	}

	std::cout << "Because of the cache loss, Testo is scheduled to run the following tests:" << std::endl;

	for (auto cache_missed: cache_missed_tests) {
		std::cout << "\t- " << cache_missed->name() << std::endl;
	}

	std::cout << "Do you confirm running them? [y/N]: ";
	std::string choice;
	std::getline(std::cin, choice);

	std::transform(choice.begin(), choice.end(), choice.begin(), ::toupper);

	if (choice != "Y" && choice != "YES") {
		throw std::runtime_error("Aborted");
	}
}

std::shared_ptr<IR::TestRun> VisitorInterpreter::add_test_to_plan(const std::shared_ptr<IR::Test>& test) {
	// если тест up-to-date и есть снепшот - то запускать его точно не надо
	if (test->is_up_to_date() && test->has_hypervisor_snapshot()) {
		return nullptr;
	}
	// попробуем определить, может быть тест недавно выполнялся
	// и все контроллеры уже в нужном состоянии
	auto it = tests_runs.rbegin();
	auto controllers = test->get_all_controllers();
	for (; it != tests_runs.rend(); ++it) {
		auto test_run = *it;
		if (test_run->test == test) {
			return test_run;
		}
		auto other_controllers = test_run->test->get_all_controllers();
		if (std::find_first_of(
				controllers.begin(), controllers.end(),
				other_controllers.begin(), other_controllers.end()
			) != controllers.end()
		) {
			break;
		}
	}
	// контроллеры точно не в нужном состоянии, но
	// если тест уже был запланирован, и тест создаст снепшоты,
	// то мы сможем восстановить состояние контроллеров
	if (test->snapshot_policy() != IR::Test::SnapshotPolicy::Never) {
		for (; it != tests_runs.rend(); ++it) {
			auto test_run = *it;
			if (test_run->test == test) {
				return test_run;
			}
		}
	}
	// всё-таки придётся запланировать тест
	auto test_run = std::make_shared<IR::TestRun>();
	test_run->test = test;
	for (auto& parent_test: test->parents) {
		auto parent_test_run = add_test_to_plan(parent_test);
		if (parent_test_run) {
			if (parent_test_run != tests_runs.back()) {
				// дополнительный прогон родительского теста не был добавлен в список
				// вместо этого мы решили восстановить состояние родительских контроллеров
				// из снепшотов
				parent_test_run->test->add_snapshot_ref(test_run.get());
			}
			test_run->parents.insert(parent_test_run);
		} else {
			// the parent test is up-to-date
			parent_test->add_snapshot_ref(test_run.get());
		}
	}
	tests_runs.push_back(test_run);
	return test_run;
}

std::list<std::shared_ptr<IR::Test>> VisitorInterpreter::get_topmost_uncached_tests() {
	TRACE();

	std::list<std::shared_ptr<IR::Test>> root_tests;
	for (auto& test: IR::program->all_selected_tests) {
		if (test->parents.size() == 0) {
			root_tests.push_back(test);
		}
	}

	std::list<std::shared_ptr<IR::Test>> result;
	std::set<std::shared_ptr<IR::Test>> visited_tests;
	std::stack<std::list<std::shared_ptr<IR::Test>>> stack;
	stack.push(std::move(root_tests));
	while (stack.size()) {
		if (stack.top().size() == 0) {
			stack.pop();
		} else {
			std::shared_ptr<IR::Test> test = stack.top().front();
			stack.top().pop_front();
			if (test->all_parents_are_up_to_date()) {
				if (test->is_up_to_date()) {
					stack.push(test->get_children());
				} else {
					result.push_back(test);
				}
			}
		}
	}
	return result;
}

struct DFSStackEntry {
	DFSStackEntry(const std::shared_ptr<IR::Test>& test_, std::set<std::string>& visited_tests_)
		: visited_tests(visited_tests_), test(test_), children_to_visit(test->get_children())
	{
	}
	DFSStackEntry(const std::list<std::shared_ptr<IR::Test>>& children_, std::set<std::string>& visited_tests_)
		: visited_tests(visited_tests_), children_to_visit(children_)
	{
	}

	std::set<std::string>& visited_tests;
	std::shared_ptr<IR::Test> test;
	std::list<std::shared_ptr<IR::Test>> children_to_visit;

	bool is_leaf() const {
		return test->children.size() == 0;
	}

	bool is_finished() const {
		return children_to_visit.size() == 0;
	}

	DFSStackEntry next() {
		if (children_to_visit.size() == 0) {
			throw std::runtime_error("Internal error: Do not call 'next()' method if `is_finished()` returns false");
		}

		auto it = std::find_if(children_to_visit.begin(), children_to_visit.end(), [&](const std::shared_ptr<IR::Test>& test) {
			return get_unresolved_dependencies(test).size() == 0;
		});

		if (it == children_to_visit.end()) {
			throw_unresolved_error();
		}

		std::shared_ptr<IR::Test> next_test = *it;
		children_to_visit.erase(it);
		visited_tests.insert(next_test->name());

		return { next_test, visited_tests };
	}

private:
	std::vector<std::string> get_unresolved_dependencies(const std::shared_ptr<IR::Test>& test) const {
		std::vector<std::string> result;
		for (const std::string& dependency: test->get_external_dependencies()) {
			if (!visited_tests.count(dependency)) {
				result.push_back(dependency);
			}
		}
		return result;
	}
	void throw_unresolved_error() const {
		std::string error_msg = "Can't decide which test to execute first because they depens on each other: ";
		int i = 0;
		for (auto test: children_to_visit) {
			if (i++) {
				error_msg += ", ";
			}
			error_msg += test->name();
		}
		throw std::runtime_error(error_msg);
	}
};

std::vector<std::shared_ptr<IR::Test>> VisitorInterpreter::get_leaf_tests_in_dfs_order(const std::list<std::shared_ptr<IR::Test>>& topmost_uncached_tests) {
	TRACE();

	std::set<std::string> visited_tests;
	for (auto& test: IR::program->all_selected_tests) {
		if (test->is_up_to_date()) {
			visited_tests.insert(test->name());
		}
	}

	std::vector<std::shared_ptr<IR::Test>> result;
	std::stack<DFSStackEntry> stack;
	stack.push({topmost_uncached_tests, visited_tests});
	while (stack.size()) {
		if (stack.top().is_finished()) {
			if (stack.top().test && stack.top().is_leaf()) {
				// in case of multiple inheritance we can reach a leaf test
				// by multiple ways, so need the next check to avoid dublicates in
				// the resulting array
				if (std::find(result.begin(), result.end(), stack.top().test) == result.end()) {
					result.push_back(stack.top().test);
				}
			}
			stack.pop();
		} else {
			stack.push({stack.top().next()});
		}
	}
	return result;
}

void VisitorInterpreter::build_test_plan() {
	TRACE();

	std::list<std::shared_ptr<IR::Test>> topmost_uncached_tests = get_topmost_uncached_tests();
	for (auto& test: topmost_uncached_tests) {
		delete_snapshot_with_children(test);
	}
	std::vector<std::shared_ptr<IR::Test>> leaf_tests_in_dfs_order = get_leaf_tests_in_dfs_order(topmost_uncached_tests);
	for (auto& test: leaf_tests_in_dfs_order) {
		add_test_to_plan(test);
	}
}

std::vector<std::shared_ptr<IR::Test>> VisitorInterpreter::bootstrap_tests_parent_first(
    const std::vector<std::shared_ptr<IR::Test>>& tests) const
{
    std::set<std::shared_ptr<IR::Test>> selected(tests.begin(), tests.end());
    std::set<std::shared_ptr<IR::Test>> visited;
    std::vector<std::shared_ptr<IR::Test>> result;
    std::function<void(const std::shared_ptr<IR::Test>&)> append = [&](const auto& test) {
        if (!selected.count(test) || visited.count(test)) {
            return;
        }
        visited.insert(test);
        for (const auto& parent: test->parents) {
            append(parent);
        }
        result.push_back(test);
    };
    for (const auto& test: tests) {
        append(test);
    }
    return result;
}

void VisitorInterpreter::run_bootstrap_setup_for_machine(
    const std::shared_ptr<IR::Machine>& machine,
    const std::vector<std::shared_ptr<IR::Test>>& bootstrap_tests)
{
    auto ordered = bootstrap_tests_parent_first(bootstrap_tests);
    if (ordered.empty()) return;

    auto& params = IR::program->stack->params;
    const std::string special = "TESTO_BOOTSTRAP_FILE_VM_NAME";
    auto old = params.find(special);
    const bool had_old = old != params.end();
    const std::string old_value = had_old ? old->second : std::string();
    params[special] = machine->name();
    coro::Finally restore_param([&] {
        if (had_old) params[special] = old_value;
        else params.erase(special);
        for (const auto& test: bootstrap_tests) {
            test->mentioned_machines.clear();
            test->mentioned_networks.clear();
            test->mentioned_flash_drives.clear();
            test->reset_cache_status();
        }
    });

    VisitorSemantic semantic(IR::program->config);
    for (const auto& test: ordered) {
        test->reset_semantic_state();
        semantic.visit_test(test);
        test->reset_cache_status();
    }

    VisitorInterpreterConfig bootstrap_config = config;
    bootstrap_config.report_folder.clear();
    bootstrap_config.junit_report.clear();
    bootstrap_config.stop_on_fail = true;
    bootstrap_config.repeat_failed = 0;
    bootstrap_config.record_tests = false;
    bootstrap_config.export_on_fail.clear();
    bootstrap_config.invalidate.clear();
    bootstrap_config.dry = false;
    VisitorInterpreter runner(bootstrap_config);
    for (const auto& test: ordered) {
        runner.add_test_to_plan(test);
    }
    if (runner.tests_runs.empty()) {
        return;
    }

    bootstrap_setup_executed = true;
    runner.reporter.init(ordered, runner.tests_runs);
    for (const auto& test_run: runner.tests_runs) {
        bool parent_failed = false;
        for (const auto& parent: test_run->parents) {
            if (parent->exec_status != IR::TestRun::ExecStatus::Passed) {
                parent_failed = true;
                break;
            }
        }
        if (parent_failed) {
            runner.reporter.skip_test();
            continue;
        }
        runner.visit_test(test_run->test, false, true, 0);
        if (test_run->exec_status != IR::TestRun::ExecStatus::Passed) {
            throw std::runtime_error("Bootstrap setup test " + test_run->test->name() + " failed");
        }
    }

    // The prepared initial state invalidates ordinary snapshots that were
    // produced from the previous _init. Current Testo reruns those tests after
    // a bootstrap cache miss instead of accepting stale normal-test cache.
    for (const auto& test: IR::program->all_selected_tests) {
        if (IR::program->is_bootstrap_test(test)) {
            continue;
        }
        auto machines = test->get_all_machines();
        if (machines.count(machine)) {
            delete_snapshot_with_children(test);
        }
    }

    runner.reporter.take_snapshot(machine, "initial");
    machine->rebase_initial_snapshot();
    for (const auto& test: IR::program->all_selected_tests) {
        test->reset_cache_status();
    }
}

void VisitorInterpreter::run_bootstrap_setups() {
    auto bootstrap_tests = IR::program->selected_bootstrap_tests();
    if (bootstrap_tests.empty()) return;

    std::set<std::shared_ptr<IR::Machine>> machines;
    for (const auto& test: IR::program->all_selected_tests) {
        if (IR::program->is_bootstrap_test(test)) continue;
        auto test_machines = test->get_all_machines();
        machines.insert(test_machines.begin(), test_machines.end());
    }
    for (const auto& machine: machines) {
        if (machine->setup_bootstrap_test()) {
            run_bootstrap_setup_for_machine(machine, bootstrap_tests);
        }
    }
}

void VisitorInterpreter::init() {
	TRACE();

	invalidate_tests();
	check_cache_missed_tests();
	build_test_plan();
}

void VisitorInterpreter::visit() {
	TRACE();

	if (!dry) {
		run_bootstrap_setups();
	}
	init();

	if (dry) {
		return;
	}

	std::vector<std::shared_ptr<IR::Test>> report_tests = IR::program->all_selected_tests;
	if (bootstrap_setup_executed) {
		report_tests.erase(std::remove_if(report_tests.begin(), report_tests.end(), [](const auto& test) {
			return IR::program->is_bootstrap_test(test);
		}), report_tests.end());
	}
	reporter.init(report_tests, tests_runs);
	const double timeout_coeff = std::stod(IR::program->resolve_top_level_param("TESTO_TIMEOUT_COEFF"));
	if (timeout_coeff != 1.0) {
		std::ostringstream value;
		value << timeout_coeff;
		reporter.report_prefix(Reporter::blue);
		reporter.report("Timeout for all actions will be multiplied by " + value.str() + "\n", Reporter::blue);
	}

	std::unique_ptr<ScreenRecorder> screen_recorder;
	coro::CoroPool recorder_pool;
	bool recorder_loop_active = false;
	coro::Finally recorder_cleanup([&] {
		recorder_loop_active = false;
		recorder_pool.cancelAll();
		recorder_pool.waitAll(true);
		screen_recorder.reset();
	});

	if (record_tests) {
		auto destination = reporter.launch_artifact_path("recording.webm");
		if (!destination.empty()) {
			std::set<std::shared_ptr<IR::Machine>> unique_machines;
			for (const auto& test_run: tests_runs) {
				auto machines = test_run->test->get_all_machines();
				unique_machines.insert(machines.begin(), machines.end());
			}
			std::vector<std::shared_ptr<IR::Machine>> machines(unique_machines.begin(), unique_machines.end());
			screen_recorder = std::make_unique<ScreenRecorder>(machines, destination);
			if (screen_recorder->active()) {
				recorder_loop_active = true;
				recorder_pool.exec([&] {
					coro::Timer timer;
					while (recorder_loop_active) {
						screen_recorder->capture_frame();
						timer.waitFor(std::chrono::milliseconds(250));
					}
				});
			}
		}
	}

	for (size_t current_test_run_index = 0; current_test_run_index < tests_runs.size(); ++current_test_run_index) {
		auto test_run = tests_runs.at(current_test_run_index);

		//Check if one of the parents failed. If it did, just fail
		bool skip_test = false;

		if (test_run->test->has_repls && skip_tests_with_repl) {
			skip_test = true;
		}

		for (auto parent: test_run->parents) {
			if (parent->exec_status != IR::TestRun::ExecStatus::Passed) {
				skip_test = true;
			}
		}
		for (auto parent: test_run->test->parents) {
			parent->remove_snapshot_ref(test_run.get());
		}

		for (auto dep: test_run->test->depends_on()) {
			// "depends_on" attribute is used to order tests, that have side effects
			// so we are interested only in the last test run of our dependency
			// (so side effects are "fresh")
			for (int64_t i = int64_t(current_test_run_index) - 1; i >= 0; --i) {
				auto finished_test_run = tests_runs.at(i);
				if (finished_test_run->test->name() == dep) {
					if (finished_test_run->exec_status != IR::TestRun::ExecStatus::Passed) {
						test_run->unsuccessful_deps_names.insert(dep);
						skip_test = true;
					}
					break;
				}
			}
		}

		if (skip_test) {
			delete_parents_hypervisor_snapshots_if_needed(test_run->test);
			reporter.skip_test();
			continue;
		}

		int retries_done = 0;
		while (true) {
			bool final_attempt = stop_on_fail || (retries_done >= repeat_failed);
			visit_test(test_run->test, retries_done > 0, final_attempt, retries_done);
			if (test_run->exec_status == IR::TestRun::ExecStatus::Passed || final_attempt) {
				break;
			}
			++retries_done;
			reporter.retry_failed_test(retries_done, repeat_failed);
			prepare_retry(test_run->test);
		}

		//We need to check if we need to stop all the vms
		//VMS should be stopped if we don't need them anymore
		//and this could happen only if there's no children tests
		//ahead

		bool need_to_stop = true;

		if (test_run->exec_status == IR::TestRun::ExecStatus::Passed) {
			for (size_t i = current_test_run_index; i < tests_runs.size(); ++i) {
				for (auto parent: tests_runs.at(i)->parents) {
					if (parent == test_run) {
						need_to_stop = false;
						break;
					}
				}
				if (!need_to_stop) {
					break;
				}
			}
		}

		if (need_to_stop) {
			stop_all_vms(test_run->test);
		}
	}

	if (record_tests) {
		fs::path destination;
		if (screen_recorder && screen_recorder->active()) {
			recorder_loop_active = false;
			recorder_pool.waitAll();
			screen_recorder->finish();
			destination = screen_recorder->destination();
		}
		reporter.report_prefix(Reporter::blue);
		reporter.report("Saved webm from vms to " + destination.generic_string() + "\n", Reporter::blue);
	}

	recorder_cleanup.discard();
	reporter.finish();
	if (reporter.get_stats(IR::TestRun::ExecStatus::Failed).size()) {
		throw TestFailedException();
	}
}

void VisitorInterpreter::delete_parents_hypervisor_snapshots_if_needed(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto parent: test->parents) {
		if (parent->can_delete_hypervisor_snaphots()) {
			for (auto controller: parent->get_all_controllers()) {
				if (controller->has_hypervisor_snapshot(parent->name())) {
					reporter.delete_hypervisor_snapshot(controller, parent->name());
					controller->delete_hypervisor_snapshot(parent->name());
					coro::CheckPoint();
				}
			}
		}
	}
}

void VisitorInterpreter::restore_parents_controllers_if_needed(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto parent: test->parents) {
		for (auto controller: parent->get_all_controllers()) {
			if (controller->current_state != parent->name()) {
				reporter.restore_snapshot(controller, parent->name());
				controller->restore_snapshot(parent->name());
				coro::CheckPoint();
			}
		}
	}
}

void VisitorInterpreter::create_networks_if_needed(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto netc: test->get_all_networks()) {
		if (netc->is_defined() &&
			netc->check_config_relevance())
		{
			continue;
		}
		netc->create();
	}
}

void VisitorInterpreter::install_new_controllers_if_needed(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto controller: test->get_all_controllers()) {
		//check if it's a new one
		auto is_new = true;
		for (auto parent: test->parents) {
			auto parent_controller = parent->get_all_controllers();
			if (parent_controller.find(controller) != parent_controller.end()) {
				//not new, go to the next vmc
				is_new = false;
				break;
			}
		}

		if (is_new) {
			//Ok, here we need to do some refactoring
			//If the config is relevant, and the init snapshot is avaliable
			//we should restore init snapshot
			//Otherwise we're creating the controller and taking init snapshot
			if (controller->is_defined() &&
				controller->has_snapshot("_init") &&
				controller->check_metadata_version() &&
				controller->check_config_relevance())
			{
				reporter.restore_snapshot(controller, "initial");
				controller->restore_snapshot("_init");
				coro::CheckPoint();
			} else {
				reporter.create_controller(controller);
				controller->create();
				reporter.take_snapshot(controller, "initial");
				controller->create_snapshot("_init", "", true);
				controller->current_state = "_init";
				coro::CheckPoint();
			}
		}
	}
}

void VisitorInterpreter::resume_parents_vms(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto parent: test->parents) {
		for (auto vmc: parent->get_all_machines()) {
			if (vmc->vm()->state() == VmState::Suspended) {
				vmc->vm()->resume();
			}
		}
	}
}

void VisitorInterpreter::suspend_all_vms(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	for (auto vmc: test->get_all_machines()) {
		if (vmc->vm()->state() == VmState::Running) {
			vmc->vm()->suspend();
		}
	}
}

void VisitorInterpreter::create_all_controllers_snapshots(const std::shared_ptr<IR::Test>& test) {
	TRACE();
	//we need to take snapshots in the right order
	//1) all the vms - so we could check that all the fds are unplugged
	for (auto controller: test->get_all_machines()) {
		if (!controller->has_snapshot(test->name(), test->is_hypervisor_snapshot_needed())) {
			reporter.take_snapshot(controller, test->name());
			controller->create_snapshot(test->name(), test->cksum, test->is_hypervisor_snapshot_needed());
			coro::CheckPoint();
		}
		controller->current_state = test->name();
	}

	//2) all the fdcs - the rest
	for (auto controller: test->get_all_flash_drives()) {
		if (!controller->has_snapshot(test->name(), test->is_hypervisor_snapshot_needed())) {
			reporter.take_snapshot(controller, test->name());
			controller->create_snapshot(test->name(), test->cksum, test->is_hypervisor_snapshot_needed());
			coro::CheckPoint();
		}
		controller->current_state = test->name();
	}
}

void VisitorInterpreter::visit_test(const std::shared_ptr<IR::Test>& test, bool retry, bool final_attempt, int retries_done) {
	TRACE();

	auto handle_failure = [&](const std::exception& error, bool honor_stop_on_fail) {
		std::stringstream ss;
		ss << test->macro_call_stack << error << std::endl;

		if (current_controller) {
			ss << std::endl << current_controller->note_was_declared_here() << "\n\n";
		}

		std::string failure_category = GetFailureCategory(error);
		reporter.test_failed(error.what(), ss.str(), failure_category, final_attempt);
		enter_repl_on_fail();
		current_controller = nullptr;

		if (!export_on_fail.empty()) {
			export_failed_state(test);
		}

		if (final_attempt) {
			if (!stop_on_fail) {
				reporter.retries_exhausted(test->name(), retries_done);
			}
			reporter.finish_failed_test();
		}

		if (honor_stop_on_fail && stop_on_fail) {
			throw std::runtime_error("");
		}
	};

	try {
		current_test = nullptr;

		reporter.prepare_environment(retry);

		restore_parents_controllers_if_needed(test);
		create_networks_if_needed(test);
		install_new_controllers_if_needed(test);

		resume_parents_vms(test);
		{
			reporter.run_test();
			StackPusher<VisitorInterpreter> pusher(this, test->stack);
			current_test = test;
			visit_command_block(test->ast_node->cmd_block);
		}
		suspend_all_vms(test);

		delete_parents_hypervisor_snapshots_if_needed(test);
		create_all_controllers_snapshots(test);

		reporter.test_passed();

	} catch (const Exception& error) {
		handle_failure(error, true);
	} catch (const std::exception& error) {
		handle_failure(error, false);
	}
}

void VisitorInterpreter::visit_command_block(const std::shared_ptr<AST::Block<AST::Cmd>>& block) {
	for (auto command: block->items) {
		visit_command(command);
	}
}

void VisitorInterpreter::visit_command(const std::shared_ptr<AST::Cmd>& cmd) {
	if (auto p = std::dynamic_pointer_cast<AST::RegularCmd>(cmd)) {
		visit_regular_command({p, stack});
	} else if (auto p = std::dynamic_pointer_cast<AST::MacroCall<AST::Cmd>>(cmd)) {
		visit_macro_call({p, stack});
	} else {
		throw std::runtime_error("Should never happen");
	}
}

void VisitorInterpreter::visit_regular_command(const IR::RegularCommand& regular_command) {
	if (auto current_controller = IR::program->get_machine_or_null(regular_command.entity())) {
		this->current_controller = current_controller;
		VisitorInterpreterActionMachine(current_controller, stack, reporter, current_test, ignore_repl, debug).visit_action(regular_command.ast_node->action);
		this->current_controller = nullptr;
	} else if (auto current_controller = IR::program->get_flash_drive_or_null(regular_command.entity())) {
		this->current_controller = current_controller;
		VisitorInterpreterActionFlashDrive(current_controller, stack, reporter, current_test, ignore_repl, debug).visit_action(regular_command.ast_node->action);
		this->current_controller = nullptr;
	} else {
		throw std::runtime_error("Should never happen");
	}
}

void VisitorInterpreter::visit_macro_call(const IR::MacroCall& macro_call) {
	reporter.macro_command_call(macro_call);
	macro_call.visit_interpreter<AST::Cmd>(this);
}

void VisitorInterpreter::visit_macro_body(const std::shared_ptr<AST::Block<AST::Cmd>>& macro_body) {
	visit_command_block(macro_body);
}

void VisitorInterpreter::export_failed_state(const std::shared_ptr<IR::Test>& test) {
	TRACE();

	reporter.report_prefix(Reporter::blue);
	reporter.report("Machine(s) snapshot for failed test " + test->name() + " saving in " + export_on_fail + ". This operation might take a while.\n", Reporter::blue);

	// Current Testo exports a temporary snapshot named after the failed test,
	// including the paused VM memory state, then removes that snapshot locally.
	suspend_all_vms(test);

	std::vector<std::shared_ptr<IR::Controller>> created;
	coro::Finally cleanup([&] {
		for (auto& controller: created) {
			try {
				if (controller->has_snapshot(test->name())) {
					controller->delete_snapshot_with_children(test->name());
				}
				controller->current_state.clear();
			} catch (...) {
			}
		}
	});

	auto create_failed_snapshot = [&](const std::shared_ptr<IR::Controller>& controller) {
		if (controller->has_snapshot(test->name())) {
			controller->delete_snapshot_with_children(test->name());
		}
		created.push_back(controller);
		reporter.take_snapshot(controller, test->name());
		controller->create_snapshot(test->name(), test->cksum, true);
		controller->current_state = test->name();
		coro::CheckPoint();
	};

	for (auto& machine: test->get_all_machines()) {
		create_failed_snapshot(machine);
	}
	for (auto& flash: test->get_all_flash_drives()) {
		create_failed_snapshot(flash);
	}

	state_transfer::export_test_state(test, export_on_fail, run_as_user, true);
	reporter.report_prefix(Reporter::blue);
	reporter.report("Snapshot saving finished.\n", Reporter::blue);
}

void VisitorInterpreter::enter_repl_on_fail() {
	if (!repl_on_fail || ignore_repl || !current_controller) {
		return;
	}

	auto repl_action = Parser(".", "repl\n", false).action();
	reporter.set_failure_repl_mode(true);
	try {
		if (auto machine = std::dynamic_pointer_cast<IR::Machine>(current_controller)) {
			VisitorInterpreterActionMachine(machine, stack, reporter, current_test, false, false).visit_action(repl_action);
		} else if (auto flash = std::dynamic_pointer_cast<IR::FlashDrive>(current_controller)) {
			VisitorInterpreterActionFlashDrive(flash, stack, reporter, current_test, false, false).visit_action(repl_action);
		}
	} catch (...) {
		reporter.set_failure_repl_mode(false);
		throw;
	}
	reporter.set_failure_repl_mode(false);
}

void VisitorInterpreter::prepare_retry(const std::shared_ptr<IR::Test>& test) {
	for (const auto& controller: test->get_all_controllers()) {
		std::string required_snapshot = "_init";
		for (const auto& parent: test->parents) {
			auto parent_controllers = parent->get_all_controllers();
			if (parent_controllers.find(controller) != parent_controllers.end()) {
				required_snapshot = parent->name();
				break;
			}
		}
		if (!controller->has_snapshot(required_snapshot, true)) {
			// A root test can fail while its controller is being created, before
			// the _init snapshot exists. Current Testo discards that partial
			// controller and recreates it on the next retry. Parent snapshots,
			// however, represent state produced by another test and cannot be
			// reconstructed here.
			if (required_snapshot != "_init") {
				throw std::runtime_error("Can't retry test " + test->name() +
					": required hypervisor snapshot " + required_snapshot + " is unavailable");
			}
			controller->undefine();
		}
		// Force the normal environment-preparation path to recreate or restore
		// the state that preceded the failed test attempt.
		controller->current_state.clear();
	}
}

void VisitorInterpreter::stop_all_vms(const std::shared_ptr<IR::Test>& test) {
	TRACE();

	for (auto vmc: test->get_all_machines()) {
		if (vmc->is_defined()) {
			if (vmc->vm()->state() != VmState::Stopped) {
				vmc->vm()->stop();
			}
			vmc->current_state = "";
		}
	}
}
