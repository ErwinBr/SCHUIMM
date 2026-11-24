#pragma once

// Shared helpers for the script tests: compile source text, run it on a scheduler with
// simulated time and collect print() output and errors.

#include "TestFramework.h"

#include "Script/Bytecode/BytecodeReader.h"
#include "Script/Bytecode/BytecodeWriter.h"
#include "Script/Compiler/Compiler.h"
#include "Script/Runtime/Program.h"
#include "Script/Runtime/ScriptScheduler.h"
#include "Script/Runtime/StandardNatives.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script::test {

// Tabs are the indentation; tests write "\t" explicitly.
struct Compiled {
    std::unique_ptr<SourceFile> source;
    CompileResult result;

    [[nodiscard]] std::string diagnostics() const { return formatDiagnostics(result, *source); }

    // True if some diagnostic contains `text` (and is on `line`, if given).
    [[nodiscard]] bool has(std::string_view text, std::uint32_t line = 0) const
    {
        for (const Diagnostic& d : result.diagnostics.all()) {
            if (d.message.find(text) != std::string::npos && (line == 0 || d.range.begin.line == line)) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool hasNote(std::string_view text) const
    {
        for (const Diagnostic& d : result.diagnostics.all()) {
            for (const std::string& note : d.notes) {
                if (note.find(text) != std::string::npos) return true;
            }
        }
        return false;
    }
};

inline Compiled compileSource(std::string text, bool optimize = true, std::string name = "test.sch")
{
    Compiled compiled;
    compiled.source = std::make_unique<SourceFile>(std::move(name), std::move(text));
    compiled.result = compile(*compiled.source, CompileOptions{optimize});
    return compiled;
}

struct RunConfig {
    bool optimize = true;
    double dt = 0.1;
    int maxUpdates = 10'000;
    std::uint64_t budget = 100'000;
    BudgetPolicy policy = BudgetPolicy::Fail;
    std::vector<std::string> entries{"main"};
    bool throughBytecodeFile = true; // Serialise and re-read (as the game does).
};

struct ScriptRun {
    bool compiled = false;
    std::string diagnostics;
    std::vector<std::string> output;
    std::vector<ScriptError> errors;
    int updates = 0;
    std::size_t stillActive = 0;

    [[nodiscard]] std::string joined() const
    {
        std::string text;
        for (const std::string& line : output) text += line + "\n";
        return text;
    }
};

// Compiles and runs `source`; each update advances time by config.dt.
inline ScriptRun runScript(std::string source, RunConfig config = {}, NativeRegistry* extraNatives = nullptr)
{
    ScriptRun run;
    Compiled compiled = compileSource(std::move(source), config.optimize);
    run.diagnostics = compiled.diagnostics();
    if (!compiled.result.success) {
        return run;
    }
    run.compiled = true;
    NativeRegistry localNatives;
    NativeRegistry& natives = extraNatives != nullptr ? *extraNatives : localNatives;
    if (!natives.find("print")) {
        REQUIRE(registerStandardNatives(natives, [&run](std::string_view, std::string_view text) {
                    run.output.emplace_back(text);
                }).has_value());
    }
    std::shared_ptr<const Program> program;
    if (config.throughBytecodeFile) {
        auto bytes = writeBytecode(compiled.result.module);
        REQUIRE(bytes.has_value());
        auto loaded = Program::load(*bytes, natives);
        if (!loaded) std::fprintf(stderr, "  load error: %s\n", loaded.error().message.c_str());
        REQUIRE(loaded.has_value());
        program = *loaded;
    } else {
        auto linked = Program::link(compiled.result.module, natives);
        REQUIRE(linked.has_value());
        program = *linked;
    }
    SchedulerSettings settings;
    settings.instructionBudget = config.budget;
    settings.budgetPolicy = config.policy;
    ScriptScheduler scheduler(settings);
    scheduler.setEventCallback([&run](const CoroutineEvent& event) {
        if (event.kind == CoroutineEvent::Kind::Failed && event.error != nullptr) {
            run.errors.push_back(*event.error);
        }
    });
    for (const std::string& entry : config.entries) {
        REQUIRE(scheduler.start(program, entry).has_value());
    }
    while (scheduler.activeCount() > 0 && run.updates < config.maxUpdates) {
        scheduler.update(run.updates == 0 ? 0.0 : config.dt);
        ++run.updates;
    }
    run.stillActive = scheduler.activeCount();
    return run;
}

// Prints output and errors of a run (for failing tests).
inline std::string describeRun(const ScriptRun& run)
{
    std::string text = run.diagnostics + run.joined();
    for (const ScriptError& error : run.errors) text += describe(error) + "\n";
    return text;
}

} // namespace schuimm::script::test
