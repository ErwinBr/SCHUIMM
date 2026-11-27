#pragma once

// The compiler pipeline in one call:
//
//   sources -> tokenize -> parse -> checkTypes -> buildIr -> [optimize] -> generateBytecode
//    (each)    (each)     (all at once: one program)                       -> validateModule
//
// compileProject() compiles the scripts of a game into one module: every source contributes to
// one game-wide namespace (Compiler.md, "Project compilation"). compile() compiles one file on
// its own, as a room script.
//
// Each stage is also usable on its own (Lexer.h, Parser.h, TypeChecker.h, IrBuilder.h,
// Optimizer.h, CodeGen.h). The compiler is independent of the game runtime and of any platform
// API; it does no file I/O (the schuimmc tool reads and writes files).

#include "Script/Bytecode/Module.h"
#include "Script/Bytecode/NativeInterface.h"
#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Ir.h"
#include "Script/Compiler/SourceFile.h"
#include "Script/Compiler/Token.h"
#include "Script/Compiler/TypeChecker.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace schuimm::script {

struct CompileOptions {
    bool optimize = true; // false: -O0, the IR is translated as built.
    // Native interfaces the script may call besides the core interface (which every script
    // sees), e.g. the adventure API. The compiler needs only their declarations; the engine binds
    // the implementations (NativeRegistry::bind). The pointers must stay valid during compile().
    std::vector<const NativeInterface*> interfaces{};
};

// One source file of a project and its role.
struct ProjectSource {
    const SourceFile* file = nullptr; // Its name is the source's path; ids must be unique.
    ScriptRole role = ScriptRole::Global;
};

// The front end's output for one source file.
struct CompiledUnit {
    const SourceFile* source = nullptr;
    ScriptRole role = ScriptRole::Room;
    std::vector<Token> tokens;
    std::unique_ptr<SourceUnit> ast;
};

// Everything the pipeline produced, for diagnostics output and debug dumps. Later stages are
// empty if an earlier stage reported errors. The tokens refer to the SourceFiles' text, so the
// SourceFiles must outlive the result.
struct CompileResult {
    bool success = false;
    Diagnostics diagnostics;
    // One per source, ordered by source name (the order the program is built in, so the output
    // does not depend on the order the sources were given in).
    std::vector<CompiledUnit> units;
    SemanticModel semantics;
    IrModule ir;          // As built.
    IrModule optimizedIr; // After optimisation (a copy of `ir` when optimisation is off).
    Module module;        // Valid bytecode if success.
};

// Compiles one file on its own, as a room script.
[[nodiscard]] CompileResult compile(const SourceFile& source, const CompileOptions& options = {});

// Compiles the sources of a game into one module. Names and ids of the sources must be unique
// (duplicates are reported as errors).
[[nodiscard]] CompileResult compileProject(std::span<const ProjectSource> sources, const CompileOptions& options = {});

// All diagnostics of a result, formatted (see formatDiagnostic), each with its own file.
[[nodiscard]] std::string formatDiagnostics(const CompileResult& result);
[[nodiscard]] std::string formatDiagnostics(const CompileResult& result, const SourceFile& source);

} // namespace schuimm::script
