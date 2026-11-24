#pragma once

#include "Core/Common/Transform2D.h"
#include "Graphics/Interface/Renderer2D.h"
#include "Graphics/Interface/Sprite.h"
#include "Graphics/Interface/Texture.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace schuimm {

// Vertex of a batched quad, as uploaded to the GPU: position in target pixels, texture
// coordinates, premultiplied RGBA8 colour and the texture slot of its batch.
struct SpriteVertex {
    float x, y;
    float u, v;
    std::uint8_t r, g, b, a;
    std::uint8_t slot;
    std::uint8_t padding[3];
};
static_assert(sizeof(SpriteVertex) == 24, "SpriteVertex must stay tightly packed");

inline constexpr int maxSpriteTextureSlots = 16;

// A run of quads drawn with one draw call. Quad i of the batch uses vertices
// [4 * (firstQuad + i), 4 * (firstQuad + i) + 4) of SpriteBatcher::vertices().
struct SpriteBatch {
    std::uint32_t firstQuad = 0;
    std::uint32_t quadCount = 0;
    int textureCount = 0;
    std::array<const Texture*, maxSpriteTextureSlots> textures{};
};

// Backend-independent part of sprite batching: turns sprites into quads, orders them and
// groups them into batches. The renderer backend only uploads vertices() and draws batches().
// No graphics API is involved, so this is tested headlessly.
//
// Ordering: quads are sorted by layer; within a layer they keep their submission order.
// Nothing else is reordered, so transparent sprites always overlap as submitted.
// Batching: consecutive quads (in that order) share a batch while their textures fit into
// the available texture slots and the batch has room; otherwise a new batch starts.
//
// Memory: internal buffers grow to their high-water mark and are then reused; clear() keeps
// their capacity, so steady-state frames do not allocate.
class SpriteBatcher {
public:
    struct Settings {
        int textureSlots = maxSpriteTextureSlots; // clamped to [1, maxSpriteTextureSlots]
        std::uint32_t maxQuadsPerBatch = 4096;     // clamped to [1, maxQuadsPerBatchLimit]
        std::uint32_t maxPendingQuads = 65536;     // isFull() once reached
    };

    // 16-bit vertex indices allow 65536 vertices, i.e. 16384 quads, per draw call.
    static constexpr std::uint32_t maxQuadsPerBatchLimit = 16384;

    SpriteBatcher();
    explicit SpriteBatcher(Settings settings);

    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    // View-space -> target-pixel transform applied to quads submitted from now on.
    void setTransform(const Transform2D& transform) noexcept { transform_ = transform; }
    [[nodiscard]] const Transform2D& transform() const noexcept { return transform_; }

    // `texture` is the texture to sample: sprite.texture, or a white texture for solid quads.
    void add(const Sprite& sprite, const Texture& texture);
    // A triangle as a degenerate quad (the last vertex repeated), sampling the centre of `white`.
    void addTriangle(const std::array<ColoredVertex, 3>& vertices, const Texture& white, int layer);

    [[nodiscard]] std::uint32_t pendingQuads() const noexcept { return static_cast<std::uint32_t>(pending_.size()); }
    [[nodiscard]] bool isEmpty() const noexcept { return pending_.empty(); }
    [[nodiscard]] bool isFull() const noexcept { return pending_.size() >= settings_.maxPendingQuads; }

    // Orders the pending quads, assigns texture slots and splits them into batches. The
    // pending quads are consumed; vertices() and batches() stay valid until the next add(),
    // build() or clear().
    void build();
    [[nodiscard]] std::span<const SpriteVertex> vertices() const noexcept { return vertices_; }
    [[nodiscard]] std::span<const SpriteBatch> batches() const noexcept { return batches_; }

    // Drops pending quads and built data; keeps the allocated capacity.
    void clear() noexcept;

    // Capacity of the internal buffers (for tests that check reuse).
    [[nodiscard]] std::size_t capacityBytes() const noexcept;

private:
    struct Quad {
        std::array<SpriteVertex, 4> vertices;
        const Texture* texture;
        int layer;
    };

    void push(const Quad& quad);

    Settings settings_;
    Transform2D transform_;
    std::vector<Quad> pending_;
    std::vector<std::uint32_t> order_;
    std::vector<SpriteVertex> vertices_;
    std::vector<SpriteBatch> batches_;
    bool needsSort_ = false;
    int maxLayer_ = 0;
};

// Straight-alpha colour -> premultiplied RGBA8, components clamped to [0, 1].
[[nodiscard]] std::array<std::uint8_t, 4> premultipliedRgba8(Color color) noexcept;

} // namespace schuimm
