#include "Core/Common/Error.h"

namespace schuimm {

const char* toString(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidState: return "InvalidState";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::IoFailure: return "IoFailure";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::PermissionDenied: return "PermissionDenied";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::InvalidData: return "InvalidData";
    case ErrorCode::PlatformFailure: return "PlatformFailure";
    case ErrorCode::GraphicsFailure: return "GraphicsFailure";
    case ErrorCode::ShaderCompilation: return "ShaderCompilation";
    }
    return "Unknown";
}

std::string describe(const Error& error)
{
    std::string text = toString(error.code);
    text += ": ";
    text += error.message;
    return text;
}

} // namespace schuimm
