#pragma once

// Navigation over a room's walkable areas: point and segment queries, destination resolution and
// shortest paths. Built once from the room's immutable walkable polygons and shared by every
// instance of the room. Knows nothing about actors, animation, rendering, input or scripts: it
// takes and returns room coordinates. Documentation/Architecture/Navigation.md describes it.
//
// Walkable domain: the closed union of the polygons. A point on a polygon's boundary is walkable,
// and a polygon edge that another polygon covers is not an obstacle. Non-walkable space (islands
// included) is whatever no polygon covers.
//
// Components: polygons that overlap or touch form one connected component; nothing is reachable
// across components.
//
// Pathfinding: a visibility graph over the domain's corner candidates (polygon vertices not
// strictly inside another polygon, and the points where edges of different polygons meet),
// searched with A* (Euclidean cost and heuristic; ties broken by node index, so results are
// deterministic). A segment is walkable only if all of it lies in the domain, which is checked
// exactly by splitting it wherever it meets a polygon edge.

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace schuimm::navigation {

inline constexpr std::size_t maxNavigationPolygons = 32;
inline constexpr std::size_t maxNavigationVertices = 256; // Over all polygons.

enum class DestinationPolicy {
    Strict,           // The destination must be walkable and reachable from the start.
    NearestReachable, // The nearest point of the start's component (click-to-walk).
};

struct NavigationPath {
    std::vector<Vec2> waypoints; // From the start to the destination; one point if they coincide.
    double length = 0.0;
};

class NavigationMap {
public:
    struct Node {
        Vec2 point;
        std::size_t component = 0;
    };
    struct Edge {
        std::size_t from = 0; // Node indices, from < to.
        std::size_t to = 0;
    };

    // `polygons` must be valid (findPolygonDefect) and within the limits above (InvalidArgument
    // otherwise).
    [[nodiscard]] static Result<std::shared_ptr<const NavigationMap>> create(std::vector<std::vector<Vec2>> polygons);

    NavigationMap(const NavigationMap&) = delete;
    NavigationMap& operator=(const NavigationMap&) = delete;

    [[nodiscard]] bool contains(Vec2 point) const noexcept;
    // The component of a walkable point; nullopt outside the domain.
    [[nodiscard]] std::optional<std::size_t> componentOf(Vec2 point) const noexcept;
    // True if every point of the segment is walkable.
    [[nodiscard]] bool isSegmentWalkable(Vec2 from, Vec2 to) const;

    // The shortest path between two walkable points of one component. NotFound if either point is
    // outside the domain or they lie in different components.
    [[nodiscard]] Result<NavigationPath> findPath(Vec2 from, Vec2 to) const;

    // The point of `component` closest to `point` (`point` itself if it lies in it). Ties go to the
    // lower polygon, then edge, index.
    [[nodiscard]] Vec2 closestPoint(Vec2 point, std::size_t component) const;

    // Where a walk from `from` towards `requested` should end. NotFound if `from` is outside the
    // domain; with Strict also if `requested` is not walkable or in another component.
    [[nodiscard]] Result<Vec2> resolveDestination(Vec2 from, Vec2 requested, DestinationPolicy policy) const;

    // Read-only data, for debugging and visualisation.
    [[nodiscard]] std::span<const std::vector<Vec2>> polygons() const noexcept { return polygons_; }
    [[nodiscard]] std::span<const Node> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::span<const Edge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::size_t componentCount() const noexcept { return componentCount_; }

private:
    struct Bounds {
        double minX, minY, maxX, maxY;
    };

    explicit NavigationMap(std::vector<std::vector<Vec2>> polygons);
    void build();
    [[nodiscard]] bool strictlyInsideOther(Vec2 point, std::size_t except, std::size_t exceptToo) const noexcept;
    [[nodiscard]] std::vector<Vec2> simplify(std::vector<Vec2> waypoints) const;

    std::vector<std::vector<Vec2>> polygons_;
    std::vector<Bounds> bounds_;
    std::vector<std::size_t> polygonComponent_;
    std::size_t componentCount_ = 0;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::vector<std::vector<std::size_t>> adjacency_; // Sorted node indices.
};

} // namespace schuimm::navigation
