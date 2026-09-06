
#include "Action.hpp"
#include "Program.hpp"
#include "OptionSeq.hpp"
#include "../resolver/Resolver.hpp"
#include "../parser/Parser.hpp"
#include "../Exceptions.hpp"

namespace IR {

std::vector<KeyboardButton> KeyCombination::buttons() const {
	if (!ast_node) {
		return {};
	}
	std::vector<KeyboardButton> result;
	for (auto& button: get_parsed()->get_buttons()) {
		result.push_back(ToKeyboardButton(button));
	}
	return result;
}

std::string KeyCombination::to_string() const {
	std::string result;
	auto buttons_ = buttons();
	for (size_t i = 0; i != buttons_.size(); ++i) {
		if (i) {
			result += "+";
		}
		result += ToString(buttons_[i]);
	}
	return result;
}

std::string Abort::message() const {
	return String(ast_node->message, stack).text();
}

std::string Bug::bug_id() const {
	return String(ast_node->bug_id, stack).text();
}

std::string Print::message() const {
	return String(ast_node->message, stack).text();
}

TimeInterval Press::interval() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("interval", "TESTO_PRESS_DEFAULT_INTERVAL");
}

KeyCombination KeySpec::combination() const {
	return {ast_node->combination, stack};
}

int32_t KeySpec::times() const {
	if (ast_node->times) {
		return Number(ast_node->times, stack).value();
	} else {
		return 1;
	}
}

KeyCombination Hold::combination() const {
	return {ast_node->combination, stack};
}

KeyCombination Release::combination() const {
	return {ast_node->combination, stack};
}

String Type::text() const {
	return {ast_node->text, stack, var_map};
}

TimeInterval Type::interval() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("interval", "TESTO_TYPE_DEFAULT_INTERVAL");
}

KeyCombination Type::autoswitch() const {
	return OptionSeq(ast_node->option_seq, stack).get<KeyCombination>("autoswitch");
}

bool Type::use_autoswitch() const {
	return OptionSeq(ast_node->option_seq, stack).has("autoswitch");
}

void Type::validate() const {
	if (!text().can_resolve_variables()) {
		return;
	}
	if (!use_autoswitch()) {
		if (!KeyboardLayout::can_be_typed_using_a_single_layout(text().str())) {
			throw ExceptionWithPos(ast_node->text->begin(), "Error: Can't type the text using a single keyboard layout. You probably should use the \"autoswitch\" option");
		}
	}
}

SelectExpr Wait::select_expr() const {
	return SelectExpr(ast_node->select_expr, stack, var_map);
}

TimeInterval Wait::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_WAIT_DEFAULT_TIMEOUT");
}

TimeInterval Wait::interval() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("interval", "TESTO_WAIT_DEFAULT_INTERVAL");
}

TimeInterval Sleep::timeout() const {
	return {ast_node->timeout, stack};
}

std::string MouseMoveClick::event_type() const {
	return ast_node->event.value();
}

std::string MouseCoordinates::x() const {
	return ast_node->dx.value();
}

std::string MouseCoordinates::y() const {
	return ast_node->dy.value();
}

bool MouseCoordinates::x_is_relative() const {
	return x().at(0) == '+' || x().at(0) == '-';
}

bool MouseCoordinates::y_is_relative() const {
	return y().at(0) == '+' || y().at(0) == '-';
}

std::string MouseSelectable::to_string() const {
	std::string result;
	if (auto p = std::dynamic_pointer_cast<AST::SelectJS>(ast_node->basic_select_expr)) {
		result += "js selection \"";
		result += IR::SelectJS(p, stack, var_map).script();
		result += "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectText>(ast_node->basic_select_expr)) {
		result += "\"";
		result += IR::SelectText(p, stack, var_map).text();
		result += "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectImg>(ast_node->basic_select_expr)) {
		result += "image \"";
		result += IR::SelectImg(p, stack, var_map).img().str();
		result += "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectImgTag>(ast_node->basic_select_expr)) {
		result += "imgtag \"";
		result += IR::SelectImgTag(p, stack, var_map).tag();
		result += "\"";
	} else {
		throw std::runtime_error("Where to go is unapplicable");
	}
	for (auto specifier: ast_node->mouse_additional_specifiers) {
		result += specifier->to_string();
	}
	return result;
}

TimeInterval MouseSelectable::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_MOUSE_MOVE_CLICK_DEFAULT_TIMEOUT");
}

std::string MouseWheel::direction() const {
	return ast_node->event.value();
}

bool MouseWheel::has_target() const {
	return ast_node->target != nullptr;
}

SelectExpr MouseWheel::target() const {
	if (!ast_node->target) throw std::runtime_error("Mouse wheel target is not specified");
	return {ast_node->target, stack, var_map};
}

std::string MouseWheel::target_to_string() const {
	if (!ast_node->target) return {};
	if (auto p = std::dynamic_pointer_cast<AST::SelectJS>(ast_node->target)) {
		return "js selection \"" + SelectJS(p, stack, var_map).script() + "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectText>(ast_node->target)) {
		return "\"" + SelectText(p, stack, var_map).text() + "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectImg>(ast_node->target)) {
		return "image \"" + SelectImg(p, stack, var_map).img().str() + "\"";
	} else if (auto p = std::dynamic_pointer_cast<AST::SelectImgTag>(ast_node->target)) {
		return "imgtag \"" + SelectImgTag(p, stack, var_map).tag() + "\"";
	}
	throw std::runtime_error("Unknown mouse wheel target type");
}

TimeInterval MouseWheel::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_MOUSEWHEEL_DEFAULT_TIMEOUT");
}

TimeInterval MouseWheel::interval() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("interval", "TESTO_MOUSEWHEEL_DEFAULT_INTERVAL");
}

int32_t MouseWheel::scroll() const {
	return OptionSeq(ast_node->option_seq, stack).get<Number>("scroll", "TESTO_MOUSEWHEEL_DEFAULT_SCROLL").value();
}

std::string SelectJS::script() const {
	return String(ast_node->str, stack, var_map).text();
}

fs::path File::path() const {
	fs::path path = text();

	if (path.is_relative()) {
		path = ast_node->begin().file.parent_path() / path;
	}

	return path;
}

std::string File::signature() const {
	if (!can_resolve_variables()) {
		return "\"" + text() + "\"";
	}
	return "\"" + path().generic_string() + "\" (file signature = " + file_signature(path()) + ")";
}

void File::validate() const {
	if (!can_resolve_variables()) {
		return;
	}

	fs::path img_path = path();

	if (!fs::exists(img_path)) {
		throw ExceptionWithPos(ast_node->begin(), "Error: specified image path does not exist: " + img_path.generic_string());
	}

	if (!fs::is_regular_file(img_path)) {
		throw ExceptionWithPos(ast_node->begin(), "Error: specified image path does not lead to a regular file: " + img_path.generic_string());
	}
}

File SelectImg::img() const {
	return {ast_node->str, stack, var_map};
}

std::string SelectImgTag::tag() const {
	return String(ast_node->str, stack, var_map).text();
}

std::string SelectText::text() const {
	return String(ast_node->str, stack, var_map).text();
}

MouseButton MouseHold::button() const {
	return ToMouseButton(ast_node->button.value());
}

bool Plug::is_on() const {
	return ast_node->is_on();
}

std::string PlugFlash::name() const {
	return Id(ast_node->name, stack).value();
}

std::string PlugNIC::name() const {
	return Id(ast_node->name, stack).value();
}

std::string PlugLink::name() const {
	return Id(ast_node->name, stack).value();
}

std::string PlugHostDev::type() const {
	return ast_node->type.value();
}

std::string PlugHostDev::addr() const {
	return String(ast_node->addr, stack).text();
}

fs::path PlugDVD::path() const {
	fs::path path = String(ast_node->path, stack).text();

	if (path.is_relative()) {
		path = ast_node->path->begin().file.parent_path() / path;
	}

	return path;
}

bool Ram::is_add() const {
	return ast_node->operation.value() == "add";
}

size_t Ram::megabytes() const {
	return Size(ast_node->size, stack).megabytes();
}

bool Cpu::is_add() const {
	return ast_node->operation.value() == "add";
}

size_t Cpu::number() const {
	return static_cast<size_t>(Number(ast_node->number, stack).value());
}

IR::TimeInterval Shutdown::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_SHUTDOWN_DEFAULT_TIMEOUT");
}

std::string Exec::interpreter() const {
	return ast_node->process.value();
}

IR::TimeInterval Exec::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_EXEC_DEFAULT_TIMEOUT");
}

static std::shared_ptr<AST::String> exec_string_option(
	const std::shared_ptr<AST::OptionSeq>& options,
	const std::string& name,
	const std::string& default_param)
{
	if (auto option = options->get(name)) {
		auto value = std::dynamic_pointer_cast<AST::String>(option->value);
		if (!value) {
			throw std::runtime_error("Failed to cast \"" + name + "\" option to string");
		}
		return value;
	}
	return Parser(".", IR::program->resolve_top_level_param(default_param), false).string();
}

std::string Exec::as() const {
	auto value = exec_string_option(ast_node->option_seq, "as", "TESTO_EXEC_DEFAULT_AS");
	return String(value, stack, var_map).text();
}

std::string Exec::expect() const {
	auto value = exec_string_option(ast_node->option_seq, "expect", "TESTO_EXEC_DEFAULT_EXPECT");
	return String(value, stack, var_map).text();
}

static std::string exec_with_value(
	const std::shared_ptr<AST::Node>& value,
	const std::shared_ptr<StackNode>& stack,
	const std::shared_ptr<VarMap>& var_map)
{
	if (auto id = std::dynamic_pointer_cast<AST::Id>(value)) {
		return Id(id, stack).value();
	}
	if (auto str = std::dynamic_pointer_cast<AST::String>(value)) {
		return String(str, stack, var_map).text();
	}
	throw std::runtime_error("Failed to cast \"with\" option to string or identifier");
}

std::string Exec::with() const {
	if (auto option = ast_node->option_seq->get("with")) {
		return exec_with_value(option->value, stack, var_map);
	}

	std::string value = IR::program->resolve_top_level_param("TESTO_EXEC_DEFAULT_WITH");
	Parser parser(".", value, false);
	if (parser.test_string()) {
		return String(parser.string(), stack, var_map).text();
	}
	return Id(parser.id(), stack).value();
}

std::string Exec::script() const {
	return String(ast_node->commands, stack, var_map).text();
}

std::string Copy::from() const {
	fs::path from = String(ast_node->from, stack).text();

	if (ast_node->is_to_guest()) {
		if (from.is_relative()) {
			from = ast_node->from->begin().file.parent_path() / from;
		}
	}

	return from.generic_string();
}

std::string Screenshot::destination() const {
	fs::path dest = String(ast_node->destination, stack).text();

	if (dest.is_relative()) {
		dest = ast_node->destination->begin().file.parent_path() / dest;
	}

	return dest.generic_string();
}

std::string Copy::to() const {
	fs::path to = String(ast_node->to, stack).text();

	if (!ast_node->is_to_guest()) {
		if (to.is_relative()) {
			to = ast_node->to->begin().file.parent_path() / to;
		}
	}

	return to.generic_string();
}

TimeInterval Copy::timeout() const {
	return OptionSeq(ast_node->option_seq, stack).get<TimeInterval>("timeout", "TESTO_COPY_DEFAULT_TIMEOUT");
}

bool Copy::nocheck() const {
	return OptionSeq(ast_node->option_seq, stack).has("nocheck");
}

std::string CycleControl::type() const {
	return ast_node->token.value();
}

}
