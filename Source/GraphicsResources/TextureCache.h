#pragma once

#include "Core/Common/Error.h"
#include "Core/Image/Image.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/Interface/Texture.h"
#include "Resource/ResourceCache.h"
#include "Resource/ResourceId.h"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace schuimm {

// GPU textures for image assets: the runtime texture resources of the engine.
//
// Data flow: image file -> ResourceCache<Image> (decoded, CPU) -> TextureCache -> Texture (GPU).
// The texture cache asks the image cache for the decoded image, so an image that is already
// decoded is reused, and creates the texture through the Renderer2D interface. It never calls
// a graphics API itself.
//
// Identity: (image resource, sampling settings). Loading the same image twice with the same
// settings returns the same texture; the same image with different settings (e.g. Nearest and
// Linear) gives two textures.
//
// Ownership: like ResourceCache, textures are shared (std::shared_ptr<const Texture>).
// unload()/unloadAll() drop the cache's reference; a holder keeps its texture alive. Every
// texture, and therefore this cache and every handle it gave out, must be destroyed before
// the GraphicsDevice; the renderer checks this when it is destroyed.
//
// The decoded image stays in the image cache; unload it there to free CPU memory once its
// texture exists. Not thread-safe.
class TextureCache {
public:
    using Handle = std::shared_ptr<const Texture>;

    // `renderer` and `images` must outlive the cache.
    TextureCache(Renderer2D& renderer, ResourceCache<Image>& images);

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Returns the cached texture or creates it. Errors keep the image cache's code (NotFound,
    // InvalidData, ...) and name the asset.
    [[nodiscard]] Result<Handle> load(const ResourceId<Image>& id, TextureSettings settings = {});

    [[nodiscard]] Handle find(const ResourceId<Image>& id, TextureSettings settings = {}) const;
    [[nodiscard]] bool isLoaded(const ResourceId<Image>& id, TextureSettings settings = {}) const;

    // Drops the cache's reference to one texture. Returns false if it was not loaded.
    bool unload(const ResourceId<Image>& id, TextureSettings settings);
    // Drops every texture made from `id`, whatever its settings. Returns how many.
    std::size_t unload(const ResourceId<Image>& id);
    void unloadAll();

    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }

private:
    struct Key {
        ResourceId<Image> id;
        TextureSettings settings;
        friend bool operator==(const Key&, const Key&) = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept;
    };

    Renderer2D& renderer_;
    ResourceCache<Image>& images_;
    std::unordered_map<Key, Handle, KeyHash> textures_;
};

} // namespace schuimm
