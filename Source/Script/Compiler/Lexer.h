#pragma once

#include "Script/Compiler/Diagnostics.h"
#include "Script/Compiler/SourceFile.h"
#include "Script/Compiler/Token.h"

#include <vector>

namespace schuimm::script {

// Hand-written lexer. Produces the complete token stream, ending with EndOfFile, and reports
// every lexical error it finds (it never stops early).
//
// Layout rules (Language.md, "Lines and indentation"):
//   * Leading tabs give the block level: one tab is one level. Spaces in leading whitespace are
//     an error, as is a line indented by more than one level deeper than the previous line.
//   * A level change emits Indent or Dedent tokens; every logical line ends with Newline.
//   * Blank lines and comment-only lines produce no tokens.
//   * Inside parentheses newlines and indentation are ignored (implicit line joining).
//   * At the end of the file a final Newline and Dedents back to level 0 are emitted.
//   * Tabs anywhere else, outside string literals and comments, are an error.
[[nodiscard]] std::vector<Token> tokenize(const SourceFile& source, Diagnostics& diagnostics);

} // namespace schuimm::script
