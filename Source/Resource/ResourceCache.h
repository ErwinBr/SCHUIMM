#pragma once

#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/ResourceId.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

namespace schuimm {

// Loads CPU-side resources of one type on request and keeps them until they are unloaded.
//
// Ownership and lifetime:
//   * The cache owns every loaded resource through a std::shared_ptr<const T>. load() and find()
//     hand out additional shared references; resources are immutable once loaded.
//   * unload(id) / unloadAll() drop the cache's reference. A resource still referenced by a
//     caller stays alive (and valid) until that caller releases it; it is then destroyed.
//     Nothing ever dangles, and unloading never has to know who still uses a resource.
//   * Destroying the cache is equivalent to unloadAll().
//   * The AssetLocator passed to the constructor must outlive the cache.
//
// Loading:
//   * load() is explicit and synchronous. A resource that is already loaded is returned without
//     touching the disk again (deduplication by ResourceId).
//   * Failures are returned to the caller and are not cached: a later load() retries, so a
//     missing file that has since been added is picked up.
//   * The loader performs only CPU work (read bytes, decode). It must not touch the GPU: GPU
//     resources such as textures are created from the decoded data by the graphics layer.
//
// Not thread-safe; use it from one thread (the main thread, today).
template <typename T>
class ResourceCache {
public:
    using Loader = std::function<Result<T>(const AssetLocator&, const AssetPath&)>;
    using Handle = std::shared_ptr<const T>;

    ResourceCache(const AssetLocator& locator, Loader loader)
        : locator_(locator)
        , loader_(std::move(loader))
    {
    }

    ResourceCache(const ResourceCache&) = delete;
    ResourceCache& operator=(const ResourceCache&) = delete;

    // Returns the cached resource, or loads it.
    [[nodiscard]] Result<Handle> load(const ResourceId<T>& id)
    {
        if (auto it = resources_.find(id); it != resources_.end()) {
            return it->second;
        }
        Result<T> loaded = loader_(locator_, id.path());
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        Handle handle = std::make_shared<const T>(std::move(*loaded));
        resources_.emplace(id, handle);
        return handle;
    }

    // The cached resource, or nullptr if it is not loaded. Never loads.
    [[nodiscard]] Handle find(const ResourceId<T>& id) const
    {
        const auto it = resources_.find(id);
        return it != resources_.end() ? it->second : nullptr;
    }

    [[nodiscard]] bool isLoaded(const ResourceId<T>& id) const { return resources_.contains(id); }

    // Drops the cache's reference. Returns false if the resource was not loaded.
    bool unload(const ResourceId<T>& id) { return resources_.erase(id) > 0; }

    void unloadAll() { resources_.clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return resources_.size(); }

private:
    const AssetLocator& locator_;
    Loader loader_;
    std::unordered_map<ResourceId<T>, Handle> resources_;
};

} // namespace schuimm
