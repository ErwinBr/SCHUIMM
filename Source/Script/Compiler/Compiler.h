#pragma once

// The compiler pipeline in one call:
//
//   source -> tokenize -> parse -> checkTypes -> buildIr -> [optimize] -> generateBytecode
//                                                                      -> validateModule
//
// Each stage is also usable on its own (Lexer.h, Parser.h, TypeChecker.h, IrBuilder.h,
// Optimizer.h, CodeGen.h). The compiler is independent of the game runtime and of any platform
// API; it does no file I/O (the schuimmc tool reads and writes files).

#include "Script/Bytecode/Module.h"
#include "Script/Compiler/Ast.h"
#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/Ir.h"
#include "Script/Compiler/SourceFile.h"
#include "Script/Compiler/Token.h"
#include "Script/Compiler/TypeChecker.h"

#include <memory>
#include <string>
#include <vector>

namespace schuimm::script {

struct CompileOptions {
    bool optimize = true; // false: -O0, the IR is translated as built.
};

// Everything the pipeline produced, for diagnostics output and debug dumps. Later stages are
// empty if an earlier stage reported errors. The tokens refer to the SourceFile's text, so the
// SourceFile must outlive the result.
struct CompileResult {
    bool success = false;
    Diagnostics diagnostics;
    std::vector<Token> tokens;
    std::unique_ptr<SourceUnit> ast;
    SemanticModel semantics;
    IrModule ir;          // As built.
    IrModule optimizedIr; // After optimisation (a copy of `ir` when optimisation is off).
    Module module;        // Valid bytecode if success.
};

[[nodiscard]] CompileResult compile(const SourceFile& source, const CompileOptions& options = {});

// All diagnostics of a result, formatted (see formatDiagnostic).
[[nodiscard]] std::string formatDiagnostics(const CompileResult& result, const SourceFile& source);

} // namespace schuimm::script
