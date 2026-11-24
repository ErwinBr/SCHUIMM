#pragma once

#include "Script/Compiler/SourceFile.h"

#include <cstddef>
#include <string>
#include <vector>

namespace schuimm::script {

enum class Severity { Error, Warning };

// The compiler stage that produced a diagnostic.
enum class DiagnosticPhase { Lexer, Parser, Semantic, CodeGen };

[[nodiscard]] const char* toString(Severity severity) noexcept;
[[nodiscard]] const char* toString(DiagnosticPhase phase) noexcept;

// One structured compiler message. Stages report through Diagnostics instead of building
// error strings; formatting happens once, in formatDiagnostic().
struct Diagnostic {
    Severity severity = Severity::Error;
    DiagnosticPhase phase = DiagnosticPhase::Parser;
    SourceRange range;
    std::string message;            // One sentence, e.g. "Undefined variable 'player_speed'."
    std::vector<std::string> notes; // Hints, e.g. "Did you mean 'False'?"
};

class Diagnostics {
public:
    void report(Severity severity, DiagnosticPhase phase, SourceRange range, std::string message,
                std::vector<std::string> notes = {});
    void error(DiagnosticPhase phase, SourceRange range, std::string message, std::vector<std::string> notes = {})
    {
        report(Severity::Error, phase, range, std::move(message), std::move(notes));
    }

    [[nodiscard]] bool hasErrors() const noexcept { return errorCount_ > 0; }
    [[nodiscard]] std::size_t errorCount() const noexcept { return errorCount_; }
    [[nodiscard]] const std::vector<Diagnostic>& all() const noexcept { return diagnostics_; }

private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t errorCount_ = 0;
};

// "scripts/main.sch:8:10: error: Undefined variable 'player_speed'." followed by the source
// line, a caret marker under the range and the notes.
[[nodiscard]] std::string formatDiagnostic(const Diagnostic& diagnostic, const SourceFile& source);

} // namespace schuimm::script
