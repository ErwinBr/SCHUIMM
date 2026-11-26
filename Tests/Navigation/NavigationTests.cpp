// Geometry predicates, polygon validation and navigation (walkable queries, destination
// resolution, pathfinding). Headless.

#include "TestFramework.h"

#include "Core/Common/GeometryPredicates.h"
#include "Core/Common/StringBuilder.h"
#include "Navigation/NavigationMap.h"
#include "Navigation/Polygon.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

using namespace schuimm;
using namespace schuimm::navigation;
using Kind = PolygonDefect::Kind;

namespace {

using Polygon = std::vector<Vec2>;

bool near(Vec2 a, Vec2 b, double tolerance = 1e-3)
{
    return distance(a, b) <= tolerance;
}

std::shared_ptr<const NavigationMap> makeMap(std::vector<Polygon> polygons)
{
    auto map = NavigationMap::create(std::move(polygons));
    if (!map) std::fprintf(stderr, "%s\n", map.error().message.c_str());
    REQUIRE(map.has_value());
    return *map;
}

// Every segment of a returned path must be walkable, and the path must start and end where asked.
void checkPath(const NavigationMap& map, const NavigationPath& path, Vec2 from, Vec2 to)
{
    REQUIRE(!path.waypoints.empty());
    CHECK(near(path.waypoints.front(), from));
    CHECK(near(path.waypoints.back(), to));
    double length = 0.0;
    for (std::size_t i = 1; i < path.waypoints.size(); ++i) {
        if (!map.isSegmentWalkable(path.waypoints[i - 1], path.waypoints[i])) {
            SCHUIMM_TEST_FAIL_(concat("segment ", i, " of the path is not walkable"), false);
        }
        CHECK(distance(path.waypoints[i - 1], path.waypoints[i]) > geometryTolerance); // No duplicates.
        length += distance(path.waypoints[i - 1], path.waypoints[i]);
    }
    CHECK(std::fabs(length - path.length) < 1e-6);
}

// A 100 x 100 square with its top-left corner at (x, y).
Polygon square(float x, float y, float size = 100.0f)
{
    return {{x, y}, {x + size, y}, {x + size, y + size}, {x, y + size}};
}

// A U shape (concave): the notch is x 40..60, y 0..80.
const Polygon uShape = {{0, 0}, {40, 0}, {40, 80}, {60, 80}, {60, 0}, {100, 0}, {100, 100}, {0, 100}};

// Four rectangles around a hole x 40..60, y 40..60 (an interior island).
std::vector<Polygon> ring()
{
    return {
        {{0, 0}, {100, 0}, {100, 40}, {0, 40}},     // top
        {{0, 60}, {100, 60}, {100, 100}, {0, 100}}, // bottom
        {{0, 30}, {40, 30}, {40, 70}, {0, 70}},     // left (overlaps top and bottom)
        {{60, 30}, {100, 30}, {100, 70}, {60, 70}}, // right
    };
}

} // namespace

// --- Vector operations and predicates ---

SCHUIMM_TEST(Geometry_VectorOperations)
{
    CHECK(dot(Vec2{1, 2}, Vec2{3, 4}) == 11.0);
    CHECK(cross(Vec2{1, 0}, Vec2{0, 1}) == 1.0);
    CHECK(length(Vec2{3, 4}) == 5.0);
    CHECK(distanceSquared(Vec2{1, 1}, Vec2{4, 5}) == 25.0);
    CHECK(normalized(Vec2{0, 5}) == (Vec2{0, 1}));
    CHECK(normalized(Vec2{0, 0}) == (Vec2{0, 0}));
    CHECK(lerp(Vec2{0, 0}, Vec2{10, 20}, 0.25) == (Vec2{2.5f, 5.0f}));
    CHECK((-Vec2{1, -2}) == (Vec2{-1, 2}));
    CHECK((Vec2{4, 6} / 2.0f) == (Vec2{2, 3}));
}

SCHUIMM_TEST(Geometry_OrientationIsADistance)
{
    CHECK(std::fabs(orientation({0, 0}, {10, 0}, {5, 3}) - 3.0) < 1e-12);
    CHECK(std::fabs(orientation({0, 0}, {1000, 0}, {5, 3}) - 3.0) < 1e-12); // Independent of edge length.
    CHECK(orientation({0, 0}, {10, 0}, {5, -3}) < 0.0);
    CHECK(orientation({1, 1}, {1, 1}, {5, 5}) == 0.0);
}

SCHUIMM_TEST(Geometry_ClosestPointOnSegment)
{
    CHECK(closestPointOnSegment({5, 5}, {0, 0}, {10, 0}).point == (Vec2{5, 0}));
    CHECK(closestPointOnSegment({-5, 5}, {0, 0}, {10, 0}).point == (Vec2{0, 0}));
    CHECK(closestPointOnSegment({15, -5}, {0, 0}, {10, 0}).point == (Vec2{10, 0}));
    CHECK(closestPointOnSegment({3, 3}, {2, 2}, {2, 2}).point == (Vec2{2, 2})); // Degenerate segment.
    CHECK(std::fabs(distanceToSegment({5, 5}, {0, 0}, {10, 0}) - 5.0) < 1e-12);
    CHECK(pointOnSegment({5, 0.0005f}, {0, 0}, {10, 0}));
    CHECK(!pointOnSegment({5, 0.01f}, {0, 0}, {10, 0}));
}

SCHUIMM_TEST(Geometry_SegmentIntersection)
{
    using SKind = SegmentIntersection::Kind;
    const auto crossing = intersectSegments({0, 0}, {10, 10}, {0, 10}, {10, 0});
    CHECK(crossing.kind == SKind::Point);
    CHECK(near(crossing.point, {5, 5}));
    CHECK(std::fabs(crossing.t - 0.5) < 1e-9);
    CHECK(intersectSegments({0, 0}, {10, 0}, {0, 1}, {10, 1}).kind == SKind::None);     // Parallel.
    CHECK(intersectSegments({0, 0}, {4, 0}, {6, 0}, {10, 0}).kind == SKind::None);      // Collinear, apart.
    const auto touch = intersectSegments({0, 0}, {10, 0}, {5, 0}, {5, 5});               // T-junction.
    CHECK(touch.kind == SKind::Point);
    CHECK(near(touch.point, {5, 0}));
    const auto endToEnd = intersectSegments({0, 0}, {5, 0}, {5, 0}, {9, 3});
    CHECK(endToEnd.kind == SKind::Point);
    const auto collinearTouch = intersectSegments({0, 0}, {5, 0}, {5, 0}, {9, 0});
    CHECK(collinearTouch.kind == SKind::Point);
    CHECK(near(collinearTouch.point, {5, 0}));
    const auto overlap = intersectSegments({0, 0}, {10, 0}, {4, 0}, {20, 0});
    CHECK(overlap.kind == SKind::Overlap);
    CHECK(std::fabs(overlap.t - 0.4) < 1e-9);
    CHECK(std::fabs(overlap.tEnd - 1.0) < 1e-9);
    CHECK(intersectSegments({0, 0}, {10, 0}, {5, 0.0005f}, {5, 5}).kind == SKind::Point); // Within tolerance.
    CHECK(intersectSegments({0, 0}, {10, 0}, {5, 0.01f}, {5, 5}).kind == SKind::None);
}

SCHUIMM_TEST(Geometry_PointInPolygon)
{
    CHECK(locatePoint({50, 90}, uShape) == PointLocation::Inside);
    CHECK(locatePoint({50, 40}, uShape) == PointLocation::Outside); // In the notch.
    CHECK(locatePoint({40, 40}, uShape) == PointLocation::Boundary);
    CHECK(locatePoint({0, 0}, uShape) == PointLocation::Boundary);  // A vertex.
    CHECK(locatePoint({150, 50}, uShape) == PointLocation::Outside);
    // A ray through a vertex counts once.
    const Polygon diamond = {{50, 0}, {100, 50}, {50, 100}, {0, 50}};
    CHECK(locatePoint({20, 50}, diamond) == PointLocation::Inside);
    CHECK(locatePoint({-20, 50}, diamond) == PointLocation::Outside);
    CHECK(signedArea(square(0, 0)) == 10000.0);
    CHECK(signedArea(Polygon{{0, 0}, {0, 100}, {100, 100}, {100, 0}}) == -10000.0);
}

// --- Polygon validation ---

SCHUIMM_TEST(Polygon_ValidShapes)
{
    CHECK(!findPolygonDefect(square(0, 0)).has_value());
    CHECK(!findPolygonDefect(Polygon{{0, 0}, {0, 100}, {100, 100}, {100, 0}}).has_value()); // Other winding.
    CHECK(!findPolygonDefect(uShape).has_value());                                           // Concave.
    // A collinear vertex that does not backtrack.
    CHECK(!findPolygonDefect(Polygon{{0, 0}, {50, 0}, {100, 0}, {100, 100}, {0, 100}}).has_value());
}

SCHUIMM_TEST(Polygon_Defects)
{
    const auto kindOf = [](const Polygon& polygon) {
        const auto defect = findPolygonDefect(polygon);
        REQUIRE(defect.has_value());
        return *defect;
    };
    CHECK(kindOf({{0, 0}, {10, 0}}).kind == Kind::TooFewVertices);
    Polygon many;
    for (std::size_t i = 0; i <= maxPolygonVertices; ++i) {
        const double angle = 6.283185307179586 * static_cast<double>(i) / static_cast<double>(maxPolygonVertices + 1);
        many.push_back({static_cast<float>(100 * std::cos(angle)), static_cast<float>(100 * std::sin(angle))});
    }
    CHECK(kindOf(many).kind == Kind::TooManyVertices);
    CHECK(kindOf({{0, 0}, {10, 0}, {std::numeric_limits<float>::infinity(), 5}}).kind == Kind::NonFinite);
    // Repeated vertex, zero-length edge, closing point repeated.
    const PolygonDefect repeated = kindOf({{0, 0}, {10, 0}, {10, 10}, {10, 0}, {0, 10}});
    CHECK(repeated.kind == Kind::RepeatedVertex);
    CHECK(repeated.first == 1);
    CHECK(repeated.second == 3);
    CHECK(kindOf({{0, 0}, {10, 0}, {10, 0}, {0, 10}}).kind == Kind::RepeatedVertex);
    const PolygonDefect closed = kindOf({{0, 0}, {10, 0}, {10, 10}, {0, 0}});
    CHECK(describe(closed, 4).find("repeats the first point") != std::string::npos);
    CHECK(kindOf({{0, 0}, {5, 5}, {10, 10}}).kind == Kind::ZeroArea);
    // A bow tie: non-zero signed area overall, but self-intersecting.
    const PolygonDefect bowTie = kindOf({{0, 0}, {100, 100}, {100, 0}, {0, 100}, {-50, 50}});
    CHECK(bowTie.kind == Kind::IntersectingEdges);
    CHECK(describe(bowTie, 5).find("intersect") != std::string::npos);
    // Adjacent edges folding back: (0,0)->(100,0)->(50,0).
    CHECK(kindOf({{0, 0}, {100, 0}, {50, 0}, {50, 50}}).kind == Kind::BacktrackingEdges);
    // Non-adjacent overlapping edges.
    CHECK(kindOf({{0, 0}, {100, 0}, {100, 50}, {60, 50}, {60, 0}, {40, 0}, {40, 50}, {0, 50}}).kind ==
          Kind::IntersectingEdges);
    // A vertex touching a non-adjacent edge (a pinch).
    CHECK(kindOf({{0, 0}, {100, 0}, {100, 100}, {50, 0}, {0, 100}}).kind == Kind::IntersectingEdges);
}

SCHUIMM_TEST(Polygon_DefectMessagesNameEdges)
{
    const PolygonDefect defect{Kind::IntersectingEdges, 1, 4};
    CHECK(describe(defect, 5) == "edges 1 (points[1]-points[2]) and 4 (points[4]-points[0]) intersect");
}

// --- Navigation map ---

SCHUIMM_TEST(Navigation_RejectsInvalidInput)
{
    CHECK(!NavigationMap::create({{{0, 0}, {100, 100}, {100, 0}, {0, 100}}}).has_value());
    std::vector<Polygon> tooMany(maxNavigationPolygons + 1, square(0, 0));
    CHECK(!NavigationMap::create(tooMany).has_value());
    CHECK(NavigationMap::create({}).has_value()); // No walkable areas: nothing is walkable.
}

SCHUIMM_TEST(Navigation_PointAndSegmentQueries)
{
    const auto map = makeMap({uShape});
    CHECK(map->contains({10, 10}));
    CHECK(map->contains({40, 40}));  // On the boundary.
    CHECK(!map->contains({50, 40})); // In the notch.
    // Both ends walkable, middle not: endpoint and midpoint checks alone would not see it.
    CHECK(!map->isSegmentWalkable({20, 20}, {80, 20}));
    CHECK(map->isSegmentWalkable({20, 90}, {80, 90}));
    // Along the boundary and through a vertex.
    CHECK(map->isSegmentWalkable({40, 0}, {40, 80}));
    CHECK(map->isSegmentWalkable({20, 90}, {60, 80}));
    CHECK(!map->isSegmentWalkable({-10, 50}, {10, 50}));
}

SCHUIMM_TEST(Navigation_DirectPath)
{
    const auto map = makeMap({square(0, 0)});
    const auto path = map->findPath({10, 10}, {90, 80});
    REQUIRE(path.has_value());
    CHECK_EQ(path->waypoints.size(), std::size_t{2});
    checkPath(*map, *path, {10, 10}, {90, 80});
    const auto same = map->findPath({10, 10}, {10, 10});
    REQUIRE(same.has_value());
    CHECK_EQ(same->waypoints.size(), std::size_t{1});
    CHECK(same->length == 0.0);
}

SCHUIMM_TEST(Navigation_AroundAConcaveBoundary)
{
    const auto map = makeMap({uShape});
    const auto path = map->findPath({20, 20}, {80, 20});
    REQUIRE(path.has_value());
    checkPath(*map, *path, {20, 20}, {80, 20});
    // Over the corners at the bottom of the notch.
    REQUIRE(path->waypoints.size() == 4);
    CHECK(near(path->waypoints[1], {40, 80}));
    CHECK(near(path->waypoints[2], {60, 80}));
    const double expected = distance({20, 20}, {40, 80}) + 20.0 + distance({60, 80}, {80, 20});
    CHECK(std::fabs(path->length - expected) < 1e-3);
}

SCHUIMM_TEST(Navigation_AroundAnInteriorIsland)
{
    const auto map = makeMap(ring());
    CHECK_EQ(map->componentCount(), std::size_t{1});
    CHECK(!map->contains({50, 50})); // The island.
    CHECK(map->contains({50, 35}));  // Covered by the top rectangle; the left one's edge is no obstacle there.
    const auto path = map->findPath({50, 20}, {50, 80});
    REQUIRE(path.has_value());
    checkPath(*map, *path, {50, 20}, {50, 80});
    CHECK(path->waypoints.size() == 4);
    for (const Vec2 p : path->waypoints) CHECK(!(p.x > 40.5f && p.x < 59.5f && p.y > 40.5f && p.y < 59.5f));
}

SCHUIMM_TEST(Navigation_OverlappingPolygonsUseIntersectionPoints)
{
    // Two overlapping rectangles forming an L: the corner of the L is where their edges cross,
    // not an authored vertex.
    const auto map = makeMap({{{0, 0}, {100, 0}, {100, 30}, {0, 30}}, {{70, 0}, {100, 0}, {100, 100}, {70, 100}}});
    bool hasCrossing = false;
    for (const auto& node : map->nodes()) hasCrossing = hasCrossing || near(node.point, {70, 30});
    CHECK(hasCrossing);
    const auto path = map->findPath({10, 15}, {85, 90});
    REQUIRE(path.has_value());
    checkPath(*map, *path, {10, 15}, {85, 90});
    REQUIRE(path->waypoints.size() == 3);
    CHECK(near(path->waypoints[1], {70, 30}));
}

SCHUIMM_TEST(Navigation_SharedEdgesAreNotObstacles)
{
    const auto map = makeMap({square(0, 0), square(100, 0)});
    CHECK_EQ(map->componentCount(), std::size_t{1});
    const auto path = map->findPath({50, 50}, {150, 50});
    REQUIRE(path.has_value());
    CHECK_EQ(path->waypoints.size(), std::size_t{2});
    checkPath(*map, *path, {50, 50}, {150, 50});
}

SCHUIMM_TEST(Navigation_DisconnectedAreas)
{
    const auto map = makeMap({square(0, 0), square(300, 0)});
    CHECK_EQ(map->componentCount(), std::size_t{2});
    const auto path = map->findPath({50, 50}, {350, 50});
    REQUIRE(!path.has_value());
    CHECK(path.error().code == ErrorCode::NotFound);
    CHECK(path.error().message.find("another walkable area") != std::string::npos);
    // Strict resolution rejects it; nearest-reachable stays in the start's area, even though the
    // other area contains the point itself.
    CHECK(!map->resolveDestination({50, 50}, {350, 50}, DestinationPolicy::Strict).has_value());
    const auto nearest = map->resolveDestination({50, 50}, {350, 50}, DestinationPolicy::NearestReachable);
    REQUIRE(nearest.has_value());
    CHECK(near(*nearest, {100, 50}));
}

SCHUIMM_TEST(Navigation_BoundaryEndpointsAndOutsidePoints)
{
    const auto map = makeMap({uShape});
    const auto path = map->findPath({0, 50}, {100, 50}); // Both on the outer boundary.
    REQUIRE(path.has_value());
    checkPath(*map, *path, {0, 50}, {100, 50});
    const auto outside = map->findPath({-10, 50}, {10, 50});
    REQUIRE(!outside.has_value());
    CHECK(outside.error().message.find("the start (-10, 50) is not walkable") != std::string::npos);
    CHECK(!map->findPath({10, 50}, {50, 40}).has_value()); // Destination in the notch.
    CHECK(!map->resolveDestination({-10, 50}, {10, 50}, DestinationPolicy::NearestReachable).has_value());
}

SCHUIMM_TEST(Navigation_NearestReachableDestination)
{
    const auto map = makeMap({uShape});
    // Inside: unchanged.
    CHECK(*map->resolveDestination({10, 10}, {90, 10}, DestinationPolicy::NearestReachable) == (Vec2{90, 10}));
    // In the notch: the nearest boundary point (the notch's side, 5 px away).
    const auto notch = map->resolveDestination({10, 10}, {45, 20}, DestinationPolicy::NearestReachable);
    REQUIRE(notch.has_value());
    CHECK(near(*notch, {40, 20}));
    // Far outside: clamped to the boundary.
    const auto below = map->resolveDestination({10, 10}, {50, 500}, DestinationPolicy::NearestReachable);
    REQUIRE(below.has_value());
    CHECK(near(*below, {50, 100}));
    CHECK(map->contains(*below));
    // Equidistant: deterministic (the lower polygon/edge index wins), the same every time.
    const auto a = map->resolveDestination({10, 10}, {50, 40}, DestinationPolicy::NearestReachable);
    const auto b = map->resolveDestination({10, 10}, {50, 40}, DestinationPolicy::NearestReachable);
    REQUIRE(a.has_value());
    CHECK(*a == *b);
    CHECK(near(*a, {40, 40}));
}

SCHUIMM_TEST(Navigation_DeterministicEqualCostRoutes)
{
    // Symmetric around the island: going left or right costs the same.
    const auto map = makeMap(ring());
    const auto first = map->findPath({50, 20}, {50, 80});
    REQUIRE(first.has_value());
    for (int i = 0; i < 5; ++i) {
        const auto again = map->findPath({50, 20}, {50, 80});
        REQUIRE(again.has_value());
        CHECK(again->waypoints == first->waypoints);
    }
    // Rebuilding the map gives the same route too.
    const auto rebuilt = makeMap(ring())->findPath({50, 20}, {50, 80});
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->waypoints == first->waypoints);
}

SCHUIMM_TEST(Navigation_PathsAreSimplified)
{
    // A collinear vertex on the way must not become a waypoint.
    const auto map = makeMap({{{0, 0}, {50, 0}, {100, 0}, {100, 100}, {0, 100}}});
    const auto path = map->findPath({0, 0}, {100, 0});
    REQUIRE(path.has_value());
    CHECK_EQ(path->waypoints.size(), std::size_t{2});
}

SCHUIMM_TEST(Navigation_GraphEdgesAreWalkable)
{
    const auto map = makeMap(ring());
    CHECK(!map->edges().empty());
    for (const auto& edge : map->edges()) {
        CHECK(edge.from < edge.to);
        CHECK(map->isSegmentWalkable(map->nodes()[edge.from].point, map->nodes()[edge.to].point));
    }
}

SCHUIMM_TEST(Navigation_ManyRandomPathsAreWalkable)
{
    // A comb: a corridor with teeth hanging down; every path between tooth tips must be walkable.
    Polygon comb = {{0, 0}, {300, 0}, {300, 40}};
    for (int tooth = 4; tooth >= 0; --tooth) {
        const float x = static_cast<float>(tooth) * 60.0f;
        comb.push_back({x + 40, 40});
        comb.push_back({x + 40, 150});
        comb.push_back({x + 20, 150});
        comb.push_back({x + 20, 40});
    }
    comb.push_back({0, 40});
    REQUIRE(!findPolygonDefect(comb).has_value());
    const auto map = makeMap({comb});
    for (int a = 0; a < 5; ++a) {
        for (int b = 0; b < 5; ++b) {
            const Vec2 from{static_cast<float>(a) * 60.0f + 30.0f, 140.0f};
            const Vec2 to{static_cast<float>(b) * 60.0f + 30.0f, 100.0f};
            const auto path = map->findPath(from, to);
            REQUIRE(path.has_value());
            checkPath(*map, *path, from, to);
        }
    }
}
