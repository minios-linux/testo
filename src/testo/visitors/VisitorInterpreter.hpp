
#pragma once
#include "../IR/Test.hpp"
#include "../IR/Action.hpp"
#include "../IR/Command.hpp"
#include "../IR/Expr.hpp"
#include "../IR/Macro.hpp"
#include "../report/Reporter.hpp"
#include "../Configs.hpp"

struct VisitorInterpreter {
	VisitorInterpreter(const VisitorInterpreterConfig& config);
	~VisitorInterpreter();

	void visit();
	void visit_test(const std::shared_ptr<IR::Test>& test, bool retry, bool final_attempt, int retries_done);
	void visit_command_block(const std::shared_ptr<AST::Block<AST::Cmd>>& block);
	void visit_command(const std::shared_ptr<AST::Cmd>& cmd);
	void visit_macro_call(const IR::MacroCall& macro_call);
	void visit_macro_body(const std::shared_ptr<AST::Block<AST::Cmd>>& macro_body);
	void visit_regular_command(const IR::RegularCommand& regular_cmd);

	std::shared_ptr<StackNode> stack;

private:
	friend struct IR::MacroCall;

	//settings
	bool stop_on_fail;
	bool repl_on_fail;
	bool debug;
	bool assume_yes;
	std::string invalidate;
	bool dry;
	bool ignore_repl;
	bool skip_tests_with_repl;
	bool record_tests;
	int repeat_failed;
	std::string export_on_fail;
	bool run_as_user;
	bool bootstrap_setup_executed = false;
	VisitorInterpreterConfig config;

	std::vector<std::shared_ptr<IR::TestRun>> tests_runs;

	void delete_snapshot_with_children(const std::shared_ptr<IR::Test>& test);
	void invalidate_tests();
	void check_cache_missed_tests();
	std::list<std::shared_ptr<IR::Test>> get_topmost_uncached_tests();
	std::vector<std::shared_ptr<IR::Test>> get_leaf_tests_in_dfs_order(const std::list<std::shared_ptr<IR::Test>>& topmost_uncached_tests);
	std::shared_ptr<IR::TestRun> add_test_to_plan(const std::shared_ptr<IR::Test>& test);
	void build_test_plan();
	void init();
	void run_bootstrap_setups();
	void run_bootstrap_setup_for_machine(const std::shared_ptr<IR::Machine>& machine, const std::vector<std::shared_ptr<IR::Test>>& bootstrap_tests);
	std::vector<std::shared_ptr<IR::Test>> bootstrap_tests_parent_first(const std::vector<std::shared_ptr<IR::Test>>& tests) const;

	std::shared_ptr<IR::Controller> current_controller;
	std::shared_ptr<IR::Test> current_test;
	Reporter reporter;

	void delete_parents_hypervisor_snapshots_if_needed(const std::shared_ptr<IR::Test>& test);
	void restore_parents_controllers_if_needed(const std::shared_ptr<IR::Test>& test);
	void create_networks_if_needed(const std::shared_ptr<IR::Test>& test);
	void install_new_controllers_if_needed(const std::shared_ptr<IR::Test>& test);
	void resume_parents_vms(const std::shared_ptr<IR::Test>& test);
	void suspend_all_vms(const std::shared_ptr<IR::Test>& test);
	void create_all_controllers_snapshots(const std::shared_ptr<IR::Test>& test);

	void stop_all_vms(const std::shared_ptr<IR::Test>& test);
	void prepare_retry(const std::shared_ptr<IR::Test>& test);
	void export_failed_state(const std::shared_ptr<IR::Test>& test);
	void enter_repl_on_fail();
};
