#include "Script/Compiler/Diagnostics.h"

#include "Core/Common/StringBuilder.h"

#include <algorithm>

namespace schuimm::script {

const char* toString(Severity severity) noexcept
{
    return severity == Severity::Error ? "error" : "warning";
}

const char* toString(DiagnosticPhase phase) noexcept
{
    switch (phase) {
    case DiagnosticPhase::Lexer: return "lexer";
    case DiagnosticPhase::Parser: return "parser";
    case DiagnosticPhase::Semantic: return "semantic";
    case DiagnosticPhase::CodeGen: return "codegen";
    }
    return "?";
}

void Diagnostics::report(Severity severity, DiagnosticPhase phase, SourceRange range, std::string message,
                         std::vector<std::string> notes)
{
    if (severity == Severity::Error) {
        ++errorCount_;
    }
    diagnostics_.push_back({severity, phase, range, std::move(message), std::move(notes)});
}

std::string formatDiagnostic(const Diagnostic& diagnostic, const SourceFile& source)
{
    const SourceLocation& at = diagnostic.range.begin;
    std::string text = concat(source.name(), ":", at.line, ":", at.column, ": ", toString(diagnostic.severity), ": ",
                              diagnostic.message, "\n");
    const std::string_view line = source.lineText(at.line);
    if (!line.empty()) {
        text += "    ";
        text += line;
        text += "\n    ";
        const std::size_t column = at.column > 0 ? at.column - 1 : 0;
        for (std::size_t i = 0; i < column && i < line.size(); ++i) {
            text += line[i] == '\t' ? '\t' : ' ';
        }
        const std::size_t length =
            diagnostic.range.end > at.offset ? std::max<std::size_t>(1, diagnostic.range.end - at.offset) : 1;
        const std::size_t visible = std::min(length, line.size() > column ? line.size() - column : std::size_t{1});
        text += std::string(std::max<std::size_t>(visible, 1), '^');
        text += "\n";
    }
    for (const std::string& note : diagnostic.notes) {
        text += "    note: " + note + "\n";
    }
    return text;
}

std::string formatDiagnostic(const Diagnostic& diagnostic, std::span<const SourceFile* const> files)
{
    const SourceFile* match = nullptr;
    for (const SourceFile* file : files) {
        if (file != nullptr && file->id() == diagnostic.range.file) {
            match = file;
            break;
        }
    }
    if (match == nullptr && !files.empty()) {
        match = files.front();
    }
    if (match == nullptr) {
        const SourceFile none("<input>", {});
        return formatDiagnostic(diagnostic, none);
    }
    return formatDiagnostic(diagnostic, *match);
}

} // namespace schuimm::script
