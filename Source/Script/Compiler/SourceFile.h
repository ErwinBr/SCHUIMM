#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::script {

// A position in a source file. Lines and columns are 1-based; columns count bytes, so a tab is
// one column. offset is the byte offset from the start of the file.
struct SourceLocation {
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

// A half-open byte range [begin.offset, end) in the file identified by `file`.
struct SourceRange {
    std::uint32_t file = 0;
    SourceLocation begin;
    std::uint32_t end = 0;
};

// Source text with its name (used in diagnostics and debug information) and a line index.
class SourceFile {
public:
    SourceFile(std::string name, std::string text, std::uint32_t id = 0);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

    // The text of a 1-based line without its line terminator; empty if out of range.
    [[nodiscard]] std::string_view lineText(std::uint32_t line) const;
    [[nodiscard]] std::size_t lineCount() const noexcept { return lineStarts_.size(); }

private:
    std::string name_;
    std::string text_;
    std::uint32_t id_;
    std::vector<std::size_t> lineStarts_;
};

} // namespace schuimm::script
