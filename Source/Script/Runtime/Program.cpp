#include "Script/Runtime/Program.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeReader.h"

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
    for (const NativeImport& import : module.imports) {
        const std::string& nativeName = module.string(import.nameIndex);
        auto function = natives.find(nativeName);
        const std::string expected = formatSignature(nativeName, import.parameters, import.returnType);
        if (!function) {
            return makeError(ErrorCode::NotFound, concat("Script '", program->name_, "' calls native function ", expected,
                                                         ", which the engine does not provide"));
        }
        if (function->parameters != import.parameters || function->returnType != import.returnType) {
            return makeError(ErrorCode::InvalidData,
                             concat("Script '", program->name_, "' declares native function ", expected,
                                    ", but the engine provides ",
                                    formatSignature(nativeName, function->parameters, function->returnType)));
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
    for (std::size_t o = 0; o < module.objects.size(); ++o) {
        for (const HandlerEntry& handler : module.objects[o].handlers) {
            program->owner_[handler.function] = static_cast<std::uint32_t>(o); // Validated: unique, in range.
        }
    }
    program->module_ = std::move(module);
    return std::shared_ptr<const Program>(std::move(program));
}

std::optional<std::uint32_t> Program::findFunction(std::string_view name) const
{
    for (std::size_t i = 0; i < module_.functions.size(); ++i) {
        if (owner_[i] == noOwner && module_.string(module_.functions[i].nameIndex) == name) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> Program::findHandler(std::string_view object, std::string_view event) const
{
    for (const ObjectEntry& entry : module_.objects) {
        if (module_.string(entry.nameIndex) != object) {
            continue;
        }
        for (const HandlerEntry& handler : entry.handlers) {
            if (module_.string(handler.nameIndex) == event) {
                return handler.function;
            }
        }
        return std::nullopt; // Object names are unique.
    }
    return std::nullopt;
}

bool Program::hasObject(std::string_view object) const
{
    return std::any_of(module_.objects.begin(), module_.objects.end(),
                       [&](const ObjectEntry& entry) { return module_.string(entry.nameIndex) == object; });
}

std::string Program::functionDisplayName(std::size_t index) const
{
    const std::string& name = functionName(index);
    if (owner_[index] == noOwner) {
        return name;
    }
    return concat(module_.string(module_.objects[owner_[index]].nameIndex), ".", name);
}

} // namespace schuimm::script
