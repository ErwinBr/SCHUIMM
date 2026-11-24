#include "Graphics/Common/SpriteBatcher.h"

#include "Core/Common/Assert.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace schuimm {

namespace {

constexpr std::size_t initialQuadCapacity = 1024;

std::uint8_t toByte(float value) noexcept
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

SpriteBatcher::Settings sanitise(SpriteBatcher::Settings settings) noexcept
{
    settings.textureSlots = std::clamp(settings.textureSlots, 1, maxSpriteTextureSlots);
    settings.maxQuadsPerBatch = std::clamp<std::uint32_t>(settings.maxQuadsPerBatch, 1, SpriteBatcher::maxQuadsPerBatchLimit);
    settings.maxPendingQuads = std::max<std::uint32_t>(settings.maxPendingQuads, 1);
    return settings;
}

} // namespace

std::array<std::uint8_t, 4> premultipliedRgba8(Color color) noexcept
{
    const float a = std::clamp(color.a, 0.0f, 1.0f);
    return {toByte(color.r * a), toByte(color.g * a), toByte(color.b * a), toByte(a)};
}

SpriteBatcher::SpriteBatcher()
    : SpriteBatcher(Settings{})
{
}

SpriteBatcher::SpriteBatcher(Settings settings)
    : settings_(sanitise(settings))
{
    const std::size_t quads = std::min<std::size_t>(initialQuadCapacity, settings_.maxPendingQuads);
    pending_.reserve(quads);
    order_.reserve(quads);
    vertices_.reserve(quads * 4);
    batches_.reserve(16);
}

void SpriteBatcher::push(const Quad& quad)
{
    // Sorting is only needed if the layers were not submitted in non-decreasing order.
    if (pending_.empty()) {
        // A new set of quads replaces the previously built output.
        vertices_.clear();
        batches_.clear();
        needsSort_ = false;
        maxLayer_ = quad.layer;
    } else if (quad.layer < maxLayer_) {
        needsSort_ = true;
    } else {
        maxLayer_ = quad.layer;
    }
    pending_.push_back(quad);
}

void SpriteBatcher::add(const Sprite& sprite, const Texture& texture)
{
    const Extent2D size = texture.size();
    const float textureWidth = static_cast<float>(std::max(1, size.width));
    const float textureHeight = static_cast<float>(std::max(1, size.height));
    const RectF source = sprite.source.value_or(RectF{0.0f, 0.0f, textureWidth, textureHeight});

    float u0 = source.x / textureWidth;
    float v0 = source.y / textureHeight;
    float u1 = (source.x + source.width) / textureWidth;
    float v1 = (source.y + source.height) / textureHeight;
    if (sprite.flipX) {
        std::swap(u0, u1);
    }
    if (sprite.flipY) {
        std::swap(v0, v1);
    }

    const RectF& d = sprite.destination;
    // Corners relative to the pivot: top-left, top-right, bottom-right, bottom-left.
    const Vec2 local[4] = {
        {-sprite.origin.x, -sprite.origin.y},
        {d.width - sprite.origin.x, -sprite.origin.y},
        {d.width - sprite.origin.x, d.height - sprite.origin.y},
        {-sprite.origin.x, d.height - sprite.origin.y},
    };
    const Vec2 pivot{d.x + sprite.origin.x, d.y + sprite.origin.y};
    Transform2D model = Transform2D::translation(pivot);
    if (sprite.rotation != 0.0f) {
        model = model * Transform2D::rotation(sprite.rotation);
    }
    const Transform2D toTarget = transform_ * model;

    const auto rgba = premultipliedRgba8(sprite.tint);
    const float u[4] = {u0, u1, u1, u0};
    const float v[4] = {v0, v0, v1, v1};

    Quad quad{};
    for (int i = 0; i < 4; ++i) {
        const Vec2 p = toTarget.apply(local[i]);
        quad.vertices[static_cast<std::size_t>(i)] = SpriteVertex{p.x, p.y, u[i], v[i], rgba[0], rgba[1], rgba[2], rgba[3], 0, {0, 0, 0}};
    }
    quad.texture = &texture;
    quad.layer = sprite.layer;
    push(quad);
}

void SpriteBatcher::addTriangle(const std::array<ColoredVertex, 3>& vertices, const Texture& white, int layer)
{
    Quad quad{};
    for (std::size_t i = 0; i < 4; ++i) {
        const ColoredVertex& source = vertices[std::min<std::size_t>(i, 2)];
        const Vec2 p = transform_.apply(source.position);
        const auto rgba = premultipliedRgba8(source.color);
        quad.vertices[i] = SpriteVertex{p.x, p.y, 0.5f, 0.5f, rgba[0], rgba[1], rgba[2], rgba[3], 0, {0, 0, 0}};
    }
    quad.texture = &white;
    quad.layer = layer;
    push(quad);
}

void SpriteBatcher::build()
{
    vertices_.clear();
    batches_.clear();
    if (pending_.empty()) {
        return;
    }

    order_.resize(pending_.size());
    for (std::uint32_t i = 0; i < order_.size(); ++i) {
        order_[i] = i;
    }
    if (needsSort_) {
        // The index breaks ties, which makes the order stable without std::stable_sort's
        // temporary buffer.
        std::sort(order_.begin(), order_.end(), [this](std::uint32_t lhs, std::uint32_t rhs) {
            const int a = pending_[lhs].layer;
            const int b = pending_[rhs].layer;
            return a != b ? a < b : lhs < rhs;
        });
    }

    SpriteBatch batch;
    const auto closeBatch = [&] {
        if (batch.quadCount > 0) {
            batches_.push_back(batch);
        }
        batch = SpriteBatch{};
        batch.firstQuad = static_cast<std::uint32_t>(vertices_.size() / 4);
    };

    for (const std::uint32_t index : order_) {
        const Quad& quad = pending_[index];
        if (batch.quadCount == settings_.maxQuadsPerBatch) {
            closeBatch();
        }
        int slot = -1;
        for (int i = 0; i < batch.textureCount; ++i) {
            if (batch.textures[static_cast<std::size_t>(i)] == quad.texture) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            if (batch.textureCount == settings_.textureSlots) {
                closeBatch();
            }
            slot = batch.textureCount++;
            batch.textures[static_cast<std::size_t>(slot)] = quad.texture;
        }
        for (SpriteVertex vertex : quad.vertices) {
            vertex.slot = static_cast<std::uint8_t>(slot);
            vertices_.push_back(vertex);
        }
        ++batch.quadCount;
    }
    closeBatch();

    pending_.clear();
    needsSort_ = false;
}

void SpriteBatcher::clear() noexcept
{
    pending_.clear();
    vertices_.clear();
    batches_.clear();
    needsSort_ = false;
}

std::size_t SpriteBatcher::capacityBytes() const noexcept
{
    return pending_.capacity() * sizeof(Quad) + order_.capacity() * sizeof(std::uint32_t) +
           vertices_.capacity() * sizeof(SpriteVertex) + batches_.capacity() * sizeof(SpriteBatch);
}

} // namespace schuimm
