#pragma once

#include <expected>
#include <string>
#include <utility>

namespace schuimm {

// Broad error categories. The message carries the specific, human-readable detail.
enum class ErrorCode {
    Unknown,
    InvalidArgument,
    InvalidState,
    Unsupported,
    IoFailure,
    NotFound,         // A file, directory or asset does not exist.
    PermissionDenied, // The operating system refused access.
    AlreadyExists,    // A file system entry of another kind is in the way.
    InvalidData,      // Content could not be parsed or decoded (e.g. a corrupt image).
    PlatformFailure,
    GraphicsFailure,
    ShaderCompilation,
};

[[nodiscard]] const char* toString(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
};

// All fallible engine operations return Result<T> (or Status when there is no value).
// Engine code does not throw; callers must inspect the result.
template <typename T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

[[nodiscard]] inline std::unexpected<Error> makeError(ErrorCode code, std::string message)
{
    return std::unexpected<Error>(Error{code, std::move(message)});
}

[[nodiscard]] std::string describe(const Error& error);

} // namespace schuimm
