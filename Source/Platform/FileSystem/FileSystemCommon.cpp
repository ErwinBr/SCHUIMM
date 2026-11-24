// Platform-independent parts of FileSystem.h, built on the native primitives.

#include "Core/Common/StringBuilder.h"
#include "Platform/FileSystem/FileSystem.h"
#include "Platform/FileSystem/Path.h"

namespace schuimm::fs {

Status createDirectories(std::string_view path)
{
    const std::string normalised = path::normalise(path);
    if (auto info = fileInfo(normalised)) {
        if (info->type == FileType::Directory) {
            return {};
        }
        return makeError(ErrorCode::AlreadyExists, concat("cannot create directory '", normalised,
                                                           "': a file of another type exists"));
    }
    const std::string parentPath = path::parent(normalised);
    if (parentPath != normalised && parentPath != ".") {
        if (auto status = createDirectories(parentPath); !status) {
            return status;
        }
    }
    return createDirectory(normalised);
}

Result<std::string> executableDirectory()
{
    auto executable = executablePath();
    if (!executable) {
        return std::unexpected(executable.error());
    }
    return path::parent(*executable);
}

} // namespace schuimm::fs
