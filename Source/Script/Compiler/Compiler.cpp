#include "Script/Compiler/Compiler.h"

#include "Script/Bytecode/BytecodeReader.h"
#include "Script/Compiler/CodeGen.h"
#include "Script/Compiler/IrBuilder.h"
#include "Script/Compiler/Lexer.h"
#include "Script/Compiler/Optimizer.h"
#include "Script/Compiler/Parser.h"

namespace schuimm::script {

CompileResult compile(const SourceFile& source, const CompileOptions& options)
{
    CompileResult result;
    result.tokens = tokenize(source, result.diagnostics);
    result.ast = parse(result.tokens, result.diagnostics);
    // The parser recovers and reports every syntax error. Semantic analysis only runs on a
    // syntactically correct file: on a recovered tree it would report follow-up errors (e.g. a
    // variable whose declaration line was malformed) that only confuse.
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    result.semantics = checkTypes(*result.ast, result.diagnostics, options.interfaces);
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    result.ir = buildIr(*result.ast, result.semantics, source.name());
    for (const IrFunction& function : result.ir.functions) {
        if (const std::string problem = verifyIr(function); !problem.empty()) {
            result.diagnostics.error(DiagnosticPhase::CodeGen, {}, "Internal compiler error (IR): " + problem);
            return result;
        }
    }
    result.optimizedIr = result.ir;
    if (options.optimize) {
        optimize(result.optimizedIr);
    }
    result.module = generateBytecode(result.optimizedIr, result.diagnostics);
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    // The compiler checks its own output with the same validator the runtime uses.
    if (auto status = validateModule(result.module); !status) {
        result.diagnostics.error(DiagnosticPhase::CodeGen, {}, "Internal compiler error: " + status.error().message);
        return result;
    }
    result.success = true;
    return result;
}

std::string formatDiagnostics(const CompileResult& result, const SourceFile& source)
{
    std::string text;
    for (const Diagnostic& diagnostic : result.diagnostics.all()) {
        text += formatDiagnostic(diagnostic, source);
    }
    return text;
}

} // namespace schuimm::script
