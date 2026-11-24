#include "Script/Runtime/Program.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeReader.h"

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
    program->module_ = std::move(module);
    return std::shared_ptr<const Program>(std::move(program));
}

std::optional<std::uint32_t> Program::findFunction(std::string_view name) const
{
    for (std::size_t i = 0; i < module_.functions.size(); ++i) {
        if (module_.string(module_.functions[i].nameIndex) == name) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

} // namespace schuimm::script
