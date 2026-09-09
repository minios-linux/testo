
#pragma once

#include "VisitorInterpreterAction.hpp"

struct VisitorInterpreterActionFlashDrive: public VisitorInterpreterAction {
	VisitorInterpreterActionFlashDrive(
		std::shared_ptr<IR::FlashDrive> fdc,
		std::shared_ptr<StackNode> stack,
		Reporter& reporter,
		std::shared_ptr<IR::Test> current_test,
		bool ignore_repl,
		bool debug,
		std::shared_ptr<SnapshotResumeContext> resume_context = nullptr
	):
		VisitorInterpreterAction(fdc, stack, reporter, current_test, ignore_repl, debug, std::move(resume_context)), fdc(fdc) {}

	~VisitorInterpreterActionFlashDrive() {}

	void visit_action(std::shared_ptr<AST::Action> action) override;
	void visit_copy(const IR::Copy& copy) override;
	bool visit_check(const IR::Check& check) override;

	std::shared_ptr<IR::FlashDrive> fdc;
};