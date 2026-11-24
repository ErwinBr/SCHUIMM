#include "GraphicsResources/TextureCache.h"

#include "Core/Common/StringBuilder.h"

#include <iterator>

namespace schuimm {

std::size_t TextureCache::KeyHash::operator()(const Key& key) const noexcept
{
    const auto settings = static_cast<std::uint64_t>(key.settings.filter) |
                          (static_cast<std::uint64_t>(key.settings.wrapU) << 4) |
                          (static_cast<std::uint64_t>(key.settings.wrapV) << 8);
    return static_cast<std::size_t>(key.id.hash() ^ (settings * 0x9E3779B97F4A7C15ull));
}

TextureCache::TextureCache(Renderer2D& renderer, ResourceCache<Image>& images)
    : renderer_(renderer)
    , images_(images)
{
}

Result<TextureCache::Handle> TextureCache::load(const ResourceId<Image>& id, TextureSettings settings)
{
    const Key key{id, settings};
    if (auto it = textures_.find(key); it != textures_.end()) {
        return it->second;
    }
    auto image = images_.load(id);
    if (!image) {
        return std::unexpected(image.error());
    }
    auto texture = renderer_.createTexture(**image, settings, TextureUsage::Static);
    if (!texture) {
        return makeError(texture.error().code,
                         concat("cannot create texture for image '", id.path().view(), "': ", texture.error().message));
    }
    Handle handle(std::move(*texture));
    textures_.emplace(key, handle);
    return handle;
}

TextureCache::Handle TextureCache::find(const ResourceId<Image>& id, TextureSettings settings) const
{
    const auto it = textures_.find(Key{id, settings});
    return it != textures_.end() ? it->second : nullptr;
}

bool TextureCache::isLoaded(const ResourceId<Image>& id, TextureSettings settings) const
{
    return textures_.contains(Key{id, settings});
}

bool TextureCache::unload(const ResourceId<Image>& id, TextureSettings settings)
{
    return textures_.erase(Key{id, settings}) > 0;
}

std::size_t TextureCache::unload(const ResourceId<Image>& id)
{
    return std::erase_if(textures_, [&](const auto& entry) { return entry.first.id == id; });
}

void TextureCache::unloadAll()
{
    textures_.clear();
}

} // namespace schuimm
