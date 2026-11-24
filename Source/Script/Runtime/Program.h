#pragma once

#include "Core/Common/Error.h"
#include "Script/Bytecode/Module.h"
#include "Script/Runtime/NativeRegistry.h"
#include "Script/Runtime/Value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

// A loaded, validated and linked script: the bytecode module, its constants as runtime values
// and the resolved native functions. Immutable after creation and shared (shared_ptr) by every
// coroutine running it, so it lives as long as its longest-running coroutine.
class Program {
public:
    // Reads and validates .schb bytes (readBytecode), then links (see below).
    [[nodiscard]] static Result<std::shared_ptr<const Program>> load(std::span<const std::byte> bytecode,
                                                                    const NativeRegistry& natives, std::string name = {});

    // Validates the module again (bytecode is never executed unvalidated, also when it comes
    // from the compiler in memory) and resolves every native import by name. Fails with
    // NotFound if a native is not registered and InvalidData if its signature differs.
    // `name` identifies the program in errors and logs; default: the module's source name.
    [[nodiscard]] static Result<std::shared_ptr<const Program>> link(Module module, const NativeRegistry& natives,
                                                                    std::string name = {});

    [[nodiscard]] const Module& module() const noexcept { return module_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Value& constant(std::size_t index) const noexcept { return constants_[index]; }
    [[nodiscard]] const NativeFunction& native(std::size_t importIndex) const noexcept { return *natives_[importIndex]; }
    [[nodiscard]] std::optional<std::uint32_t> findFunction(std::string_view name) const;
    [[nodiscard]] const std::string& functionName(std::size_t index) const { return module_.string(module_.functions[index].nameIndex); }

private:
    Program() = default;

    Module module_;
    std::string name_;
    std::vector<Value> constants_;
    std::vector<std::shared_ptr<const NativeFunction>> natives_;
};

} // namespace schuimm::script
