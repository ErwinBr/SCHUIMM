// Win32 implementation of FileSystem.h. Paths arrive as UTF-8, are normalised, converted to
// backslashes and UTF-16, and passed to the wide-character (W) APIs. Absolute paths that would
// exceed MAX_PATH get the "\\?\" prefix so long paths work without a manifest opt-in.

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"

#include <algorithm>

#include <windows.h>

namespace schuimm::fs {

namespace {

// Converts UTF-8 to UTF-16; fails on invalid UTF-8 instead of substituting characters.
Result<std::wstring> toWide(std::string_view utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (length <= 0) {
        return makeError(ErrorCode::InvalidArgument, concat("path is not valid UTF-8: '", utf8, "'"));
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(),
                        length);
    return result;
}

std::string toUtf8(std::wstring_view wide)
{
    if (wide.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), length, nullptr,
                        nullptr);
    return result;
}

std::string systemMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string text = length > 0 && buffer != nullptr ? toUtf8(std::wstring_view(buffer, length)) : std::string();
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return concat(text.empty() ? std::string("error") : text, " (", error, ")");
}

Error systemError(DWORD error, std::string_view operation, std::string_view path)
{
    ErrorCode code = ErrorCode::IoFailure;
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_DIRECTORY: code = ErrorCode::NotFound; break;
    case ERROR_ACCESS_DENIED: code = ErrorCode::PermissionDenied; break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: code = ErrorCode::AlreadyExists; break;
    case ERROR_INVALID_NAME: code = ErrorCode::InvalidArgument; break;
    default: break;
    }
    return Error{code, concat(operation, " '", path, "': ", systemMessage(error))};
}

// UTF-8 generic path -> UTF-16 native path.
Result<std::wstring> nativePath(std::string_view path)
{
    std::string normalised = path::normalise(path, path::PathStyle::Windows);
    const bool absolute = path::isAbsolute(normalised, path::PathStyle::Windows);
    std::replace(normalised.begin(), normalised.end(), '/', '\\');
    auto wide = toWide(normalised);
    if (!wide) {
        return wide;
    }
    if (absolute && wide->size() >= MAX_PATH - 12) { // CreateDirectoryW's limit is MAX_PATH - 12.
        if (wide->starts_with(L"\\\\")) {
            *wide = L"\\\\?\\UNC\\" + wide->substr(2);
        } else {
            *wide = L"\\\\?\\" + *wide;
        }
    }
    return wide;
}

FileType typeOf(DWORD attributes)
{
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return FileType::Directory;
    }
    if ((attributes & FILE_ATTRIBUTE_DEVICE) != 0) {
        return FileType::Other;
    }
    return FileType::Regular;
}

class Handle {
public:
    explicit Handle(HANDLE handle) noexcept : handle_(handle) {}
    ~Handle()
    {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_;
};

} // namespace

Result<FileInfo> fileInfo(std::string_view path)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(native->c_str(), GetFileExInfoStandard, &data)) {
        return std::unexpected(systemError(GetLastError(), "cannot inspect", path));
    }
    FileInfo info;
    info.type = typeOf(data.dwFileAttributes);
    if (info.type == FileType::Regular) {
        info.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    return info;
}

Result<std::vector<DirectoryEntry>> listDirectory(std::string_view path)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    // For a file, FindFirstFileExW reports ERROR_DIRECTORY or ERROR_PATH_NOT_FOUND depending on
    // the Windows version; check first so the result matches the POSIX implementation.
    const DWORD attributes = GetFileAttributesW(native->c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::unexpected(systemError(GetLastError(), "cannot open directory", path));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return makeError(ErrorCode::NotFound, concat("cannot open directory '", path, "': not a directory"));
    }

    std::wstring pattern = *native;
    if (!pattern.empty() && pattern.back() != L'\\') {
        pattern += L'\\';
    }
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return std::unexpected(systemError(GetLastError(), "cannot open directory", path));
    }
    std::vector<DirectoryEntry> entries;
    DWORD error = ERROR_SUCCESS;
    do {
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") {
            continue;
        }
        entries.push_back({toUtf8(name), typeOf(data.dwFileAttributes)});
    } while (FindNextFileW(find, &data));
    error = GetLastError();
    FindClose(find);
    if (error != ERROR_NO_MORE_FILES) {
        return std::unexpected(systemError(error, "cannot read directory", path));
    }
    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
    return entries;
}

Result<std::vector<std::byte>> readFile(std::string_view path, std::uint64_t maxBytes)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    const DWORD attributes = GetFileAttributesW(native->c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return makeError(ErrorCode::InvalidArgument, concat("cannot read '", path, "': not a regular file"));
    }
    const Handle file(CreateFileW(native->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        return std::unexpected(systemError(GetLastError(), "cannot open file", path));
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file.get(), &fileSize)) {
        return std::unexpected(systemError(GetLastError(), "cannot inspect", path));
    }
    const auto size = static_cast<std::uint64_t>(fileSize.QuadPart);
    if (size > maxBytes) {
        return makeError(ErrorCode::InvalidArgument,
                         concat("cannot read '", path, "': ", size, " bytes exceeds the limit of ", maxBytes));
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1u << 30));
        DWORD count = 0;
        if (!ReadFile(file.get(), data.data() + offset, request, &count, nullptr)) {
            return std::unexpected(systemError(GetLastError(), "cannot read", path));
        }
        if (count == 0) {
            data.resize(offset); // The file shrank while being read.
            break;
        }
        offset += count;
    }
    return data;
}

Status writeFile(std::string_view path, std::span<const std::byte> data)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    const Handle file(CreateFileW(native->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        return std::unexpected(systemError(GetLastError(), "cannot create file", path));
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, 1u << 30));
        DWORD count = 0;
        if (!WriteFile(file.get(), data.data() + offset, request, &count, nullptr)) {
            return std::unexpected(systemError(GetLastError(), "cannot write", path));
        }
        offset += count;
    }
    return {};
}

Status createDirectory(std::string_view path)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    if (CreateDirectoryW(native->c_str(), nullptr)) {
        return {};
    }
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        const DWORD attributes = GetFileAttributesW(native->c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return {};
        }
        return makeError(ErrorCode::AlreadyExists,
                         concat("cannot create directory '", path, "': a file of another type exists"));
    }
    return std::unexpected(systemError(error, "cannot create directory", path));
}

Status remove(std::string_view path)
{
    auto native = nativePath(path);
    if (!native) {
        return std::unexpected(native.error());
    }
    const DWORD attributes = GetFileAttributesW(native->c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::unexpected(systemError(GetLastError(), "cannot remove", path));
    }
    const BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryW(native->c_str())
                                                                       : DeleteFileW(native->c_str());
    if (!removed) {
        return std::unexpected(systemError(GetLastError(), "cannot remove", path));
    }
    return {};
}

Result<std::string> executablePath()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::unexpected(systemError(GetLastError(), "cannot query", "executable path"));
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return path::normalise(toUtf8(buffer), path::PathStyle::Windows);
        }
        buffer.resize(buffer.size() * 2);
    }
}

} // namespace schuimm::fs
