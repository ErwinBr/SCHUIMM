#include "Script/Compiler/Compiler.h"

#include "Core/Common/StringBuilder.h"
#include "Script/Bytecode/BytecodeReader.h"
#include "Script/Compiler/CodeGen.h"
#include "Script/Compiler/IrBuilder.h"
#include "Script/Compiler/Lexer.h"
#include "Script/Compiler/Optimizer.h"
#include "Script/Compiler/Parser.h"

#include <algorithm>
#include <unordered_map>

namespace schuimm::script {

CompileResult compile(const SourceFile& source, const CompileOptions& options)
{
    const ProjectSource project{&source, ScriptRole::Room};
    return compileProject(std::span<const ProjectSource>(&project, 1), options);
}

CompileResult compileProject(std::span<const ProjectSource> sources, const CompileOptions& options)
{
    CompileResult result;
    std::vector<ProjectSource> ordered(sources.begin(), sources.end());
    std::erase_if(ordered, [](const ProjectSource& source) { return source.file == nullptr; });
    std::stable_sort(ordered.begin(), ordered.end(), [](const ProjectSource& a, const ProjectSource& b) {
        return a.file->name() < b.file->name();
    });
    if (ordered.empty()) {
        result.diagnostics.error(DiagnosticPhase::Parser, {}, "No source files to compile.");
        return result;
    }
    std::unordered_map<std::uint32_t, const SourceFile*> ids;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const SourceFile& file = *ordered[i].file;
        SourceRange start;
        start.file = file.id();
        start.begin = {0, 1, 1};
        if (i > 0 && ordered[i - 1].file->name() == file.name()) {
            result.diagnostics.error(DiagnosticPhase::Parser, start, concat("The source '", file.name(), "' is given twice."));
        } else if (const auto existing = ids.find(file.id()); existing != ids.end()) {
            result.diagnostics.error(DiagnosticPhase::Parser, start,
                                     concat("The sources '", existing->second->name(), "' and '", file.name(),
                                            "' have the same file id ", file.id(), "."));
        }
        ids.emplace(file.id(), &file);
    }
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    for (const ProjectSource& source : ordered) {
        CompiledUnit unit;
        unit.source = source.file;
        unit.role = source.role;
        unit.tokens = tokenize(*source.file, result.diagnostics);
        unit.ast = parse(unit.tokens, result.diagnostics);
        result.units.push_back(std::move(unit));
    }
    // The parser recovers and reports every syntax error. Semantic analysis only runs on
    // syntactically correct files: on a recovered tree it would report follow-up errors (e.g. a
    // variable whose declaration line was malformed) that only confuse.
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    std::vector<SourceUnitInput> inputs;
    for (const CompiledUnit& unit : result.units) {
        inputs.push_back({unit.ast.get(), unit.source->name(), unit.role, unit.source->id()});
    }
    result.semantics = checkTypes(inputs, result.diagnostics, options.interfaces);
    if (result.diagnostics.hasErrors()) {
        return result;
    }
    result.ir = buildIr(result.semantics);
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

std::string formatDiagnostics(const CompileResult& result)
{
    std::vector<const SourceFile*> files;
    for (const CompiledUnit& unit : result.units) {
        files.push_back(unit.source);
    }
    std::string text;
    for (const Diagnostic& diagnostic : result.diagnostics.all()) {
        text += formatDiagnostic(diagnostic, files);
    }
    return text;
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
