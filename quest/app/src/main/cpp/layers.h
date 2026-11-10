#pragma once

// The interface in space. The overlay texture (painted in Kotlin) covers the picture and its
// margins; it is shown as several quads, each a crop of that texture at its own depth, so the
// controls float in front of the picture and the rails turn towards the operator like a
// cockpit. The renderer draws these layers and the pointer is intersected with the same
// ones, so what is seen is what is hit.

#include "gl_renderer.h"
#include "hud_layout.h"
#include "xr_math.h"

#include <array>

namespace imago::quest {

struct Layer {
    XrPosef pose;          // centre of the quad; +Z faces the operator
    float width = 0;       // metres
    float height = 0;
    float x0 = 0, y0 = 0;  // the crop, in picture units ((0, 0) top-left of the picture)
    float x1 = 1, y1 = 1;
    float depth = 0;       // metres in front of the picture, for the drawing order
};

constexpr float kPictureLayerDepth = 0.004f; // boxes and badges, a hair in front
constexpr float kBarDepth = 0.02f;
constexpr float kRailDepth = 0.025f;
constexpr float kSheetDepth = 0.06f;         // the picture's layer while a sheet is open
constexpr float kRailAngleDeg = 14.0f;       // each rail turned in about its inner edge

/// The five layers, back to front. sheetOpen: a sheet (Connections) is over the picture.
inline std::array<Layer, 5> buildLayers(const Panel& panel, bool sheetOpen) {
    const float w = panel.width;
    const float h = panel.height;
    const auto flat = [&](float x0, float y0, float x1, float y1, float depth) {
        Layer l;
        l.x0 = x0; l.y0 = y0; l.x1 = x1; l.y1 = y1;
        l.width = (x1 - x0) * w;
        l.height = (y1 - y0) * h;
        l.depth = depth;
        const XrVector3f local{((x0 + x1) / 2 - 0.5f) * w, (0.5f - (y0 + y1) / 2) * h, depth};
        l.pose.orientation = panel.pose.orientation;
        l.pose.position = panel.pose.position + rotate(panel.pose.orientation, local);
        return l;
    };
    // A rail hinged on the picture's edge: its outer edge comes towards the operator.
    const auto rail = [&](bool left) {
        const float railWidth = hud::kMarginSide * w;
        Layer l;
        l.x0 = left ? -hud::kMarginSide : 1.0f;
        l.x1 = left ? 0.0f : 1.0f + hud::kMarginSide;
        l.y0 = 0; l.y1 = 1;
        l.width = railWidth;
        l.height = h;
        l.depth = kRailDepth;
        const XrQuaternionf turn = yawRotation(left ? kRailAngleDeg : -kRailAngleDeg);
        const XrVector3f hinge{left ? -w / 2 : w / 2, 0, kRailDepth};
        const XrVector3f centre = hinge + rotate(turn, XrVector3f{left ? -railWidth / 2 : railWidth / 2, 0, 0});
        l.pose.orientation = multiply(panel.pose.orientation, turn);
        l.pose.position = panel.pose.position + rotate(panel.pose.orientation, centre);
        return l;
    };
    const float side = hud::kMarginSide;
    std::array<Layer, 5> layers = {
        flat(0, 0, 1, 1, sheetOpen ? kSheetDepth : kPictureLayerDepth),
        flat(-side, -hud::kMarginTop, 1 + side, 0, kBarDepth),
        flat(-side, 1, 1 + side, 1 + hud::kMarginBottom, kBarDepth),
        rail(true),
        rail(false),
    };
    if (sheetOpen) {
        // The sheet is the frontmost thing: draw its layer last.
        const Layer sheet = layers[0];
        for (size_t k = 0; k + 1 < layers.size(); ++k) {
            layers[k] = layers[k + 1];
        }
        layers.back() = sheet;
    }
    return layers;
}

/// Where a ray meets a layer: t along the ray and the point in picture units. False if it
/// misses, or comes from behind.
inline bool intersect(const Layer& layer, XrVector3f origin, XrVector3f direction, float& t, float& x, float& y) {
    const XrVector3f normal = rotate(layer.pose.orientation, XrVector3f{0, 0, 1});
    const float facing = dot(direction, normal);
    if (facing >= -1e-4f) {
        return false;
    }
    t = dot(layer.pose.position - origin, normal) / facing;
    if (t <= 0) {
        return false;
    }
    const XrVector3f local = origin + direction * t - layer.pose.position;
    const float lx = dot(local, rotate(layer.pose.orientation, XrVector3f{1, 0, 0})) / layer.width;
    const float ly = dot(local, rotate(layer.pose.orientation, XrVector3f{0, 1, 0})) / layer.height;
    if (lx < -0.5f || lx > 0.5f || ly < -0.5f || ly > 0.5f) {
        return false;
    }
    x = layer.x0 + (lx + 0.5f) * (layer.x1 - layer.x0);
    y = layer.y0 + (0.5f - ly) * (layer.y1 - layer.y0);
    return true;
}

} // namespace imago::quest
