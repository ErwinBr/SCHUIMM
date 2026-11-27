#include "Script/Runtime/Program.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeReader.h"
#include "Script/Bytecode/Disassembler.h"

#include <algorithm>

namespace schuimm::script {

Result<std::shared_ptr<const Program>> Program::load(std::span<const std::byte> bytecode, const NativeRegistry& natives,
                                                     std::string name)
{
    auto module = readBytecode(bytecode);
    if (!module) {
        return std::unexpected(module.error());
    }
    return link(std::move(*module), natives, std::move(name));
}

Result<std::shared_ptr<const Program>> Program::link(Module module, const NativeRegistry& natives, std::string name)
{
    if (auto status = validateModule(module); !status) {
        return std::unexpected(status.error());
    }
    std::shared_ptr<Program> program(new Program());
    program->name_ = name.empty() ? module.sourceName() : std::move(name);
    // Engine enums: the module's ordinals must mean what the engine's do, so the registered enum
    // of that qualified name must have the same members in the same order.
    std::vector<std::string> qualifiedEnums(module.enums.size());
    for (std::size_t e = 0; e < module.enums.size(); ++e) {
        const EnumEntry& entry = module.enums[e];
        if (!entry.isEngine()) {
            continue;
        }
        qualifiedEnums[e] = qualifiedEnumName(module.string(entry.qualifierIndex), module.string(entry.nameIndex));
        const std::vector<std::string>* members = natives.findEnum(qualifiedEnums[e]);
        if (members == nullptr) {
            return makeError(ErrorCode::NotFound, concat("Script '", program->name_, "' uses engine enum '", qualifiedEnums[e],
                                                         "', which the engine does not provide"));
        }
        bool same = members->size() == entry.members.size();
        for (std::size_t m = 0; same && m < entry.members.size(); ++m) {
            same = (*members)[m] == module.string(entry.members[m]);
        }
        if (!same) {
            return makeError(ErrorCode::InvalidData,
                             concat("Script '", program->name_, "' was compiled against a different version of enum '",
                                    qualifiedEnums[e], "' than the engine provides; recompile the script from its source"));
        }
    }
    const auto sameType = [&](TypeRef imported, const NativeType& provided) {
        if (imported.kind != provided.kind) return false;
        return imported.kind != ScriptType::Enum || qualifiedEnums[imported.enumIndex] == provided.enumName;
    };
    for (const NativeImport& import : module.imports) {
        const std::string& nativeName = module.string(import.nameIndex);
        auto function = natives.find(nativeName);
        std::string expected = nativeName + "(";
        for (std::size_t i = 0; i < import.parameters.size(); ++i) {
            expected += concat(i == 0 ? "" : ", ", formatType(module, import.parameters[i]));
        }
        expected += concat(") -> ", formatType(module, import.returnType));
        if (!function) {
            return makeError(ErrorCode::NotFound, concat("Script '", program->name_, "' calls native function ", expected,
                                                         ", which the engine does not provide"));
        }
        bool same = function->parameters.size() == import.parameters.size() && sameType(import.returnType, function->returnType);
        for (std::size_t i = 0; same && i < import.parameters.size(); ++i) {
            same = sameType(import.parameters[i], function->parameters[i]);
        }
        if (!same) {
            return makeError(ErrorCode::InvalidData, concat("Script '", program->name_, "' declares native function ", expected,
                                                            ", but the engine provides ", formatSignature(*function)));
        }
        program->natives_.push_back(std::move(function));
    }
    program->constants_.reserve(module.constants.size());
    for (const Constant& constant : module.constants) {
        switch (constant.type) {
        case ScriptType::Int: program->constants_.push_back(Value::fromInt(constant.intValue)); break;
        case ScriptType::Float: program->constants_.push_back(Value::fromFloat(constant.floatValue)); break;
        default: program->constants_.push_back(Value::fromString(module.string(constant.stringIndex))); break;
        }
    }
    program->owner_.assign(module.functions.size(), noOwner);
    for (std::size_t b = 0; b < module.behaviours.size(); ++b) {
        for (const HandlerEntry& handler : module.behaviours[b].handlers) {
            program->owner_[handler.function] = static_cast<std::uint32_t>(b); // Validated: unique, in range.
        }
    }
    program->module_ = std::move(module);
    return std::shared_ptr<const Program>(std::move(program));
}

std::optional<std::uint32_t> Program::findFunction(std::string_view name) const
{
    for (std::size_t i = 0; i < module_.functions.size(); ++i) {
        // Compiler-generated functions ("spawn:walk_to") are internal: their names contain ':'.
        const std::string& candidate = module_.string(module_.functions[i].nameIndex);
        if (owner_[i] == noOwner && candidate == name && candidate.find(':') == std::string::npos) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> Program::findBehaviour(BehaviourKind kind, std::string_view source,
                                                   std::string_view name) const
{
    for (std::size_t b = 0; b < module_.behaviours.size(); ++b) {
        const BehaviourEntry& entry = module_.behaviours[b];
        if (entry.kind != kind || (!source.empty() && module_.sourcePath(entry.source) != source)) {
            continue;
        }
        if (kind == BehaviourKind::Object && module_.string(entry.nameIndex) != name) {
            continue;
        }
        return static_cast<std::uint32_t>(b);
    }
    return std::nullopt;
}

std::optional<std::uint32_t> Program::findHandler(std::uint32_t behaviour, std::string_view event) const
{
    if (behaviour >= module_.behaviours.size()) {
        return std::nullopt;
    }
    for (const HandlerEntry& handler : module_.behaviours[behaviour].handlers) {
        if (module_.string(handler.nameIndex) == event) {
            return handler.function;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> Program::findHandler(std::string_view object, std::string_view event) const
{
    const auto behaviour = findBehaviour(BehaviourKind::Object, {}, object);
    return behaviour ? findHandler(*behaviour, event) : std::nullopt;
}

bool Program::hasObject(std::string_view object) const
{
    return findBehaviour(BehaviourKind::Object, {}, object).has_value();
}

std::string Program::functionDisplayName(std::size_t index) const
{
    const std::string& name = functionName(index);
    if (owner_[index] == noOwner) {
        return name;
    }
    const BehaviourEntry& behaviour = module_.behaviours[owner_[index]];
    if (behaviour.kind == BehaviourKind::Object) {
        return concat(module_.string(behaviour.nameIndex), ".", name);
    }
    return concat(toString(behaviour.kind), ".", name);
}

const std::string& Program::functionSource(std::size_t index) const
{
    const std::uint32_t source = module_.functions[index].source;
    return source < module_.sources.size() ? module_.sourcePath(source) : name_;
}

} // namespace schuimm::script
