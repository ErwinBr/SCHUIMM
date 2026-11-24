#include "Script/Compiler/SourceFile.h"

namespace schuimm::script {

SourceFile::SourceFile(std::string name, std::string text, std::uint32_t id)
    : name_(std::move(name))
    , text_(std::move(text))
    , id_(id)
{
    lineStarts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n' || (text_[i] == '\r' && (i + 1 >= text_.size() || text_[i + 1] != '\n'))) {
            lineStarts_.push_back(i + 1);
        }
    }
}

std::string_view SourceFile::lineText(std::uint32_t line) const
{
    if (line == 0 || line > lineStarts_.size()) {
        return {};
    }
    const std::size_t begin = lineStarts_[line - 1];
    std::size_t end = begin;
    while (end < text_.size() && text_[end] != '\n' && text_[end] != '\r') {
        ++end;
    }
    return std::string_view(text_).substr(begin, end - begin);
}

} // namespace schuimm::script
