#include "Platform/FileSystem/Path.h"

#include <algorithm>
#include <vector>

namespace schuimm::path {

namespace {

bool isDriveLetter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string toGeneric(std::string_view path, PathStyle style)
{
    std::string result(path);
    if (style == PathStyle::Windows) {
        for (char& c : result) {
            if (c == '\\') {
                c = '/';
            }
        }
        // "\\?\C:\x" and "\\?\UNC\server\share" are Win32 long-path spellings of the same paths.
        if (result.starts_with("//?/UNC/")) {
            result = "//" + result.substr(8);
        } else if (result.starts_with("//?/")) {
            result = result.substr(4);
        }
    }
    return result;
}

// Splits a generic path into its root and the rest (without leading separators).
struct Split {
    std::string root;
    std::string_view rest;
};

Split splitRoot(std::string_view generic, PathStyle style)
{
    if (style == PathStyle::Windows) {
        if (generic.size() >= 2 && isDriveLetter(generic[0]) && generic[1] == ':') {
            std::string root;
            root += static_cast<char>(generic[0] >= 'a' ? generic[0] - 'a' + 'A' : generic[0]);
            root += ':';
            std::string_view rest = generic.substr(2);
            if (!rest.empty() && rest.front() == '/') {
                root += '/';
            }
            return {root, rest};
        }
        if (generic.size() > 2 && generic[0] == '/' && generic[1] == '/' && generic[2] != '/') {
            // UNC: //server/share/
            const std::size_t serverEnd = generic.find('/', 2);
            if (serverEnd != std::string_view::npos) {
                std::size_t shareEnd = generic.find('/', serverEnd + 1);
                if (shareEnd == std::string_view::npos) {
                    shareEnd = generic.size();
                }
                if (shareEnd > serverEnd + 1) {
                    return {std::string(generic.substr(0, shareEnd)) + "/", generic.substr(shareEnd)};
                }
            }
        }
    }
    if (!generic.empty() && generic.front() == '/') {
        return {"/", generic};
    }
    return {"", generic};
}

} // namespace

std::string rootOf(std::string_view path, PathStyle style)
{
    const std::string generic = toGeneric(path, style);
    return splitRoot(generic, style).root;
}

bool isAbsolute(std::string_view path, PathStyle style)
{
    const std::string root = rootOf(path, style);
    if (style == PathStyle::Posix) {
        return root == "/";
    }
    // Windows: "C:/" or "//server/share/"; "/" and "C:" depend on process state.
    return root.size() > 2 && root.back() == '/';
}

std::string normalise(std::string_view path, PathStyle style)
{
    const std::string generic = toGeneric(path, style);
    const Split split = splitRoot(generic, style);
    const bool rooted = !split.root.empty() && split.root.back() == '/';

    std::vector<std::string_view> components;
    std::string_view rest = split.rest;
    while (!rest.empty()) {
        const std::size_t end = rest.find('/');
        const std::string_view component = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            if (!components.empty() && components.back() != "..") {
                components.pop_back();
            } else if (!rooted) {
                components.push_back(component);
            }
            continue;
        }
        components.push_back(component);
    }

    std::string result = split.root;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i > 0) {
            result += '/';
        }
        result += components[i];
    }
    if (result.empty()) {
        result = ".";
    }
    return result;
}

std::string join(std::string_view base, std::string_view relative, PathStyle style)
{
    if (isAbsolute(relative, style) || base.empty()) {
        return normalise(relative, style);
    }
    std::string combined(base);
    combined += '/';
    combined += relative;
    return normalise(combined, style);
}

std::string makeAbsolute(std::string_view path, std::string_view base, PathStyle style)
{
    if (isAbsolute(path, style)) {
        return normalise(path, style);
    }
    if (!isAbsolute(base, style)) {
        return {};
    }
    return join(base, path, style);
}

std::string parent(std::string_view path, PathStyle style)
{
    const std::string normalised = normalise(path, style);
    const std::string root = rootOf(normalised, style);
    if (normalised == root) {
        return normalised;
    }
    const std::size_t slash = normalised.rfind('/');
    const std::string_view last = std::string_view(normalised).substr(slash == std::string::npos ? 0 : slash + 1);
    if (last == "..") {
        return normalised + "/..";
    }
    if (normalised == ".") {
        return "..";
    }
    if (slash == std::string::npos || slash < root.size()) {
        return root.empty() ? std::string(".") : root;
    }
    return normalised.substr(0, slash);
}

std::string fileName(std::string_view path, PathStyle style)
{
    const std::string normalised = normalise(path, style);
    const std::string root = rootOf(normalised, style);
    if (normalised == root || normalised == ".") {
        return {};
    }
    const std::size_t slash = normalised.rfind('/');
    const std::size_t start = slash == std::string::npos ? root.size() : slash + 1;
    return normalised.substr(std::max(start, root.size()));
}

std::string extension(std::string_view path, PathStyle style)
{
    const std::string name = fileName(path, style);
    if (name == "..") {
        return {};
    }
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return {};
    }
    return name.substr(dot);
}

} // namespace schuimm::path
