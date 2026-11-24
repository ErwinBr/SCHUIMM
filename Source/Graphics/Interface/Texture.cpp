#include "Graphics/Interface/Texture.h"

namespace schuimm {

const char* toString(TextureFilter filter) noexcept
{
    switch (filter) {
    case TextureFilter::Nearest: return "Nearest";
    case TextureFilter::Linear: return "Linear";
    }
    return "?";
}

const char* toString(TextureWrap wrap) noexcept
{
    switch (wrap) {
    case TextureWrap::ClampToEdge: return "ClampToEdge";
    case TextureWrap::Repeat: return "Repeat";
    case TextureWrap::MirroredRepeat: return "MirroredRepeat";
    }
    return "?";
}

} // namespace schuimm
