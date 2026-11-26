#include "Script/Bytecode/NativeInterface.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>
#include <set>

namespace schuimm::script {

namespace {

bool isIdentifier(std::string_view text) noexcept
{
    if (text.empty() || (text[0] >= '0' && text[0] <= '9')) return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string typeName(const NativeType& type)
{
    return type.kind == ScriptType::Enum ? type.enumName : std::string(toString(type.kind));
}

} // namespace

const char* toString(NativeCategory category) noexcept
{
    switch (category) {
    case NativeCategory::Immediate: return "immediate";
    case NativeCategory::Suspending: return "suspending";
    case NativeCategory::Background: return "background";
    }
    return "?";
}

const EnumDeclaration* NativeInterface::findEnum(std::string_view enumName) const noexcept
{
    for (const EnumDeclaration& declaration : enums) {
        if (declaration.name == enumName) return &declaration;
    }
    return nullptr;
}

const NativeDeclaration* NativeInterface::findFunction(std::string_view functionName) const noexcept
{
    for (const NativeDeclaration& declaration : functions) {
        if (declaration.name == functionName) return &declaration;
    }
    return nullptr;
}

std::string qualifiedEnumName(std::string_view interfaceName, std::string_view enumName)
{
    return concat(interfaceName, ".", enumName);
}

std::string formatSignature(const NativeDeclaration& declaration)
{
    std::string text = declaration.name + "(";
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
        text += concat(i == 0 ? "" : ", ", typeName(declaration.parameters[i]));
    }
    return concat(text, ") -> ", typeName(declaration.returnType));
}

std::string checkInterface(const NativeInterface& interface)
{
    if (!isIdentifier(interface.name)) return concat("interface name '", interface.name, "' is not an identifier");
    std::set<std::string_view> enumNames;
    for (const EnumDeclaration& declaration : interface.enums) {
        if (!isIdentifier(declaration.name)) return concat("enum name '", declaration.name, "' is not an identifier");
        if (!enumNames.insert(declaration.name).second) return concat("duplicate enum '", declaration.name, "'");
        if (declaration.members.empty()) return concat("enum '", declaration.name, "' has no members");
        if (declaration.members.size() > 0x10000) return concat("enum '", declaration.name, "' has too many members");
        std::set<std::string_view> members;
        for (const std::string& member : declaration.members) {
            if (!isIdentifier(member)) return concat("member '", member, "' of enum '", declaration.name, "' is not an identifier");
            if (!members.insert(member).second) return concat("duplicate member '", member, "' in enum '", declaration.name, "'");
        }
    }
    const auto checkType = [&](const NativeType& type, const std::string& where) -> std::string {
        if (type.kind == ScriptType::Enum) {
            if (interface.findEnum(type.enumName) == nullptr) {
                return concat(where, " uses enum '", type.enumName, "', which the interface does not declare");
            }
        } else if (!type.enumName.empty()) {
            return concat(where, " names an enum for a non-enum type");
        }
        return {};
    };
    std::set<std::string_view> names;
    for (const NativeDeclaration& declaration : interface.functions) {
        if (!isIdentifier(declaration.name)) return concat("function name '", declaration.name, "' is not an identifier");
        if (!names.insert(declaration.name).second) return concat("duplicate function '", declaration.name, "'");
        if (declaration.parameters.size() > 255) return concat("function '", declaration.name, "' has too many parameters");
        for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
            const NativeType& parameter = declaration.parameters[i];
            if (parameter.kind == ScriptType::None) return concat("parameter ", i + 1, " of '", declaration.name, "' has type None");
            if (auto problem = checkType(parameter, concat("parameter ", i + 1, " of '", declaration.name, "'")); !problem.empty()) {
                return problem;
            }
        }
        if (declaration.returnType.kind == ScriptType::Any || declaration.returnType.kind == ScriptType::Entity) {
            return concat("function '", declaration.name, "' cannot return ", toString(declaration.returnType.kind));
        }
        if (auto problem = checkType(declaration.returnType, concat("the result of '", declaration.name, "'")); !problem.empty()) {
            return problem;
        }
    }
    return {};
}

const NativeInterface& coreInterface()
{
    static const NativeInterface core = [] {
        NativeInterface interface;
        interface.name = "core";
        using enum ScriptType;
        const auto add = [&](std::string name, std::vector<NativeType> parameters, NativeType result,
                             NativeCategory category, bool pure, std::string documentation) {
            interface.functions.push_back(
                {std::move(name), std::move(parameters), std::move(result), category, pure, std::move(documentation)});
        };
        add("print", {Any}, None, NativeCategory::Immediate, false, "Writes a value as text to the script output.");
        add("wait", {Float}, None, NativeCategory::Suspending, false,
            "Suspends the coroutine for the given number of seconds of game time.");
        add("yield", {}, None, NativeCategory::Suspending, false, "Suspends the coroutine until the next scheduler update.");
        add("length", {Vec2}, Float, NativeCategory::Immediate, true, "The length of a vector.");
        add("distance", {Vec2, Vec2}, Float, NativeCategory::Immediate, true, "The distance between two points.");
        add("normalize", {Vec2}, Vec2, NativeCategory::Immediate, true,
            "The vector scaled to length 1; vec2(0, 0) for a zero (or non-finite) vector.");
        add("lerp", {Vec2, Vec2, Float}, Vec2, NativeCategory::Immediate, true,
            "a + (b - a) * t (t is not clamped).");
        return interface;
    }();
    return core;
}

} // namespace schuimm::script
