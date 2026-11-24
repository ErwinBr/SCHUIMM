#pragma once

// A Renderer2D without a GPU, for headless tests of code that talks to the renderer
// interface (application loop, texture cache, text drawing). It records what it is asked to do.

#include "Core/Common/StringBuilder.h"
#include "Graphics/Interface/Renderer2D.h"

#include <memory>
#include <vector>

namespace schuimm::test {

class FakeTexture final : public Texture {
public:
    FakeTexture(const TextureDescription& description, int& liveCounter)
        : size_(description.size)
        , settings_(description.settings)
        , usage_(description.usage)
        , liveCounter_(liveCounter)
    {
        ++liveCounter_;
        if (!description.pixels.empty()) {
            const std::size_t rowBytes = static_cast<std::size_t>(size_.width) * 4;
            const std::size_t stride = description.rowStride == 0 ? rowBytes : description.rowStride;
            for (int y = 0; y < size_.height; ++y) {
                const auto row = description.pixels.subspan(stride * static_cast<std::size_t>(y), rowBytes);
                pixels.insert(pixels.end(), row.begin(), row.end());
            }
        } else {
            pixels.assign(static_cast<std::size_t>(size_.width) * static_cast<std::size_t>(size_.height) * 4, 0);
        }
    }
    ~FakeTexture() override { --liveCounter_; }

    FakeTexture(const FakeTexture&) = delete;
    FakeTexture& operator=(const FakeTexture&) = delete;

    Extent2D size() const override { return size_; }
    PixelFormat format() const override { return PixelFormat::RGBA8; }
    TextureSettings settings() const override { return settings_; }
    TextureUsage usage() const override { return usage_; }

    std::vector<std::uint8_t> pixels; // as uploaded (not premultiplied)
    int updates = 0;

private:
    Extent2D size_;
    TextureSettings settings_;
    TextureUsage usage_;
    int& liveCounter_;
};

class FakeRenderTarget final : public RenderTarget {
public:
    FakeRenderTarget(Extent2D size, int& textureCounter)
        : texture_(TextureDescription{size, PixelFormat::RGBA8, {}, 0, AlphaMode::Premultiplied, {}, TextureUsage::Dynamic},
                   textureCounter)
    {
    }
    Extent2D size() const override { return texture_.size(); }
    const Texture& texture() const override { return texture_; }

private:
    FakeTexture texture_;
};

class FakeRenderer final : public Renderer2D {
public:
    int clears = 0;
    int texturesCreated = 0;
    int liveTextures = 0;
    int internalTextures = 0; // owned by render targets
    int flushes = 0;
    int maxTextureSize = 4096;
    std::vector<Sprite> sprites;
    Transform2D currentView;
    std::vector<const RenderTarget*> targetStack;

    Extent2D virtualResolution() const override { return {640, 360}; }
    Extent2D targetSize() const override { return targetStack.empty() ? virtualResolution() : targetStack.back()->size(); }
    int textureSlots() const override { return 16; }
    void clear(Color) override { ++clears; }
    void drawSprite(const Sprite& sprite) override { sprites.push_back(sprite); }
    void drawTriangle(const std::array<ColoredVertex, 3>&, int) override {}
    void flush() override { ++flushes; }
    void setView(const Transform2D& view) override { currentView = view; }
    const Transform2D& view() const override { return currentView; }

    using Renderer2D::createTexture;
    Result<std::unique_ptr<Texture>> createTexture(const TextureDescription& description) override
    {
        if (description.size.isEmpty() || description.size.width > maxTextureSize || description.size.height > maxTextureSize) {
            return makeError(ErrorCode::InvalidArgument, "invalid texture size");
        }
        if (!description.pixels.empty()) {
            if (auto status = validatePixelLayout(description.size, description.pixels.size(), description.rowStride); !status) {
                return std::unexpected(status.error());
            }
        }
        ++texturesCreated;
        return std::unique_ptr<Texture>(std::make_unique<FakeTexture>(description, liveTextures));
    }

    Status updateTexture(Texture& texture, TextureRegion region, std::span<const std::uint8_t> pixels, std::size_t rowStride,
                         AlphaMode) override
    {
        auto& fake = static_cast<FakeTexture&>(texture);
        if (fake.usage() != TextureUsage::Dynamic) {
            return makeError(ErrorCode::InvalidState, "only dynamic textures can be updated");
        }
        const Extent2D size = fake.size();
        if (auto status = validateRegion(size, region); !status) {
            return status;
        }
        if (auto status = validatePixelLayout({region.width, region.height}, pixels.size(), rowStride); !status) {
            return status;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(region.width) * 4;
        const std::size_t stride = rowStride == 0 ? rowBytes : rowStride;
        for (int y = 0; y < region.height; ++y) {
            for (std::size_t x = 0; x < rowBytes; ++x) {
                const std::size_t target =
                    (static_cast<std::size_t>(region.y + y) * static_cast<std::size_t>(size.width) + static_cast<std::size_t>(region.x)) * 4 + x;
                fake.pixels[target] = pixels[stride * static_cast<std::size_t>(y) + x];
            }
        }
        ++fake.updates;
        return {};
    }

    Result<std::unique_ptr<RenderTarget>> createRenderTarget(const RenderTargetDescription& description) override
    {
        if (description.size.isEmpty()) {
            return makeError(ErrorCode::InvalidArgument, "invalid render target size");
        }
        return std::unique_ptr<RenderTarget>(std::make_unique<FakeRenderTarget>(description.size, internalTextures));
    }
    void beginRenderTarget(RenderTarget& target) override { targetStack.push_back(&target); }
    void endRenderTarget() override { targetStack.pop_back(); }
    RenderStatistics lastFrameStatistics() const override { return {}; }
};

} // namespace schuimm::test
