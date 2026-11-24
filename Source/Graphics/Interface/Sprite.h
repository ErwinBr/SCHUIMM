#pragma once

#include "Core/Common/Geometry.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Texture.h"

#include <optional>

namespace schuimm {

// One textured quad, drawn through the sprite batch (Renderer2D::drawSprite).
//
// Coordinates are in the current view space (Renderer2D::setView): world units when a camera
// transform is set, target pixels otherwise.
struct Sprite {
    // nullptr draws a solid quad in `tint`.
    const Texture* texture = nullptr;

    // Unrotated rectangle: top-left corner and size.
    RectF destination;

    // Part of the texture to show, in texels (origin top-left). nullopt = the whole texture.
    std::optional<RectF> source;

    // Pivot for rotation, relative to destination's top-left corner (e.g. {w/2, h/2} rotates
    // around the centre).
    Vec2 origin{};

    // Radians; positive turns clockwise on screen (y points down).
    float rotation = 0.0f;

    // Multiplies the texel colour, including alpha. Straight alpha, like Color everywhere.
    Color tint = Color::white();

    // Mirror the texture horizontally / vertically within the destination.
    bool flipX = false;
    bool flipY = false;

    // Drawing order: lower layers first. Sprites on the same layer are drawn in the order they
    // were submitted. Ordering applies between flushes (see Renderer2D).
    int layer = 0;
};

} // namespace schuimm
