#include "Navigation/NavigationMap.h"

#include "Core/Common/GeometryPredicates.h"
#include "Core/Common/StringBuilder.h"
#include "Navigation/Polygon.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>

namespace schuimm::navigation {

namespace {

std::string describePoint(Vec2 p)
{
    return concat("(", p.x, ", ", p.y, ")");
}

class UnionFind {
public:
    explicit UnionFind(std::size_t n)
        : parent_(n)
    {
        std::iota(parent_.begin(), parent_.end(), std::size_t{0});
    }
    std::size_t find(std::size_t i)
    {
        while (parent_[i] != i) i = parent_[i] = parent_[parent_[i]];
        return i;
    }
    void unite(std::size_t a, std::size_t b)
    {
        a = find(a);
        b = find(b);
        if (a != b) parent_[std::max(a, b)] = std::min(a, b); // The lower index is the root.
    }

private:
    std::vector<std::size_t> parent_;
};

bool overlaps(double aMin, double aMax, double bMin, double bMax) noexcept
{
    return aMin <= bMax + geometryTolerance && bMin <= aMax + geometryTolerance;
}

} // namespace

Result<std::shared_ptr<const NavigationMap>> NavigationMap::create(std::vector<std::vector<Vec2>> polygons)
{
    if (polygons.size() > maxNavigationPolygons) {
        return makeError(ErrorCode::InvalidArgument, concat("at most ", maxNavigationPolygons, " walkable areas, found ",
                                                            polygons.size()));
    }
    std::size_t vertices = 0;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        if (const auto defect = findPolygonDefect(polygons[i])) {
            return makeError(ErrorCode::InvalidArgument,
                             concat("walkable area ", i, ": ", describe(*defect, polygons[i].size())));
        }
        vertices += polygons[i].size();
    }
    if (vertices > maxNavigationVertices) {
        return makeError(ErrorCode::InvalidArgument, concat("at most ", maxNavigationVertices,
                                                            " walkable-area points in a room, found ", vertices));
    }
    std::shared_ptr<NavigationMap> map(new NavigationMap(std::move(polygons)));
    map->build();
    return std::shared_ptr<const NavigationMap>(std::move(map));
}

NavigationMap::NavigationMap(std::vector<std::vector<Vec2>> polygons)
    : polygons_(std::move(polygons))
{
}

void NavigationMap::build()
{
    const std::size_t count = polygons_.size();
    bounds_.reserve(count);
    for (const auto& polygon : polygons_) {
        Bounds b{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                 std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
        for (const Vec2 p : polygon) {
            b.minX = std::min(b.minX, static_cast<double>(p.x));
            b.minY = std::min(b.minY, static_cast<double>(p.y));
            b.maxX = std::max(b.maxX, static_cast<double>(p.x));
            b.maxY = std::max(b.maxY, static_cast<double>(p.y));
        }
        bounds_.push_back(b);
    }

    // Edge-pair intersections between different polygons: they join components and are corner
    // candidates of the union's boundary.
    UnionFind components(count);
    std::vector<Vec2> meetings;
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            const Bounds& a = bounds_[i];
            const Bounds& b = bounds_[j];
            if (!overlaps(a.minX, a.maxX, b.minX, b.maxX) || !overlaps(a.minY, a.maxY, b.minY, b.maxY)) continue;
            const auto& p = polygons_[i];
            const auto& q = polygons_[j];
            bool joined = false;
            for (std::size_t e = 0; e < p.size(); ++e) {
                for (std::size_t f = 0; f < q.size(); ++f) {
                    const SegmentIntersection meet =
                        intersectSegments(p[e], p[(e + 1) % p.size()], q[f], q[(f + 1) % q.size()]);
                    if (meet.kind == SegmentIntersection::Kind::None) continue;
                    joined = true;
                    meetings.push_back(meet.point);
                    if (meet.kind == SegmentIntersection::Kind::Overlap) {
                        meetings.push_back(lerp(p[e], p[(e + 1) % p.size()], meet.tEnd));
                    }
                }
            }
            // One inside the other without touching edges.
            if (joined || locatePoint(q[0], p) != PointLocation::Outside ||
                locatePoint(p[0], q) != PointLocation::Outside) {
                components.unite(i, j);
            }
        }
    }
    // Number components by their lowest polygon index.
    std::vector<std::size_t> number(count, count);
    polygonComponent_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t root = components.find(i);
        if (number[root] == count) number[root] = componentCount_++;
        polygonComponent_[i] = number[root];
    }

    // Corner candidates, de-duplicated within the tolerance, in a fixed order.
    const auto addNode = [this](Vec2 point) {
        for (const Node& node : nodes_) {
            if (distance(node.point, point) <= geometryTolerance) return;
        }
        const auto component = componentOf(point);
        if (component) nodes_.push_back(Node{point, *component});
    };
    for (std::size_t i = 0; i < count; ++i) {
        for (const Vec2 vertex : polygons_[i]) {
            if (!strictlyInsideOther(vertex, i, i)) addNode(vertex);
        }
    }
    for (const Vec2 point : meetings) {
        if (!strictlyInsideOther(point, count, count)) addNode(point);
    }

    adjacency_.resize(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        for (std::size_t j = i + 1; j < nodes_.size(); ++j) {
            if (nodes_[i].component != nodes_[j].component) continue;
            if (!isSegmentWalkable(nodes_[i].point, nodes_[j].point)) continue;
            edges_.push_back(Edge{i, j});
            adjacency_[i].push_back(j);
            adjacency_[j].push_back(i);
        }
    }
    for (auto& neighbours : adjacency_) std::sort(neighbours.begin(), neighbours.end());
}

bool NavigationMap::strictlyInsideOther(Vec2 point, std::size_t except, std::size_t exceptToo) const noexcept
{
    for (std::size_t i = 0; i < polygons_.size(); ++i) {
        if (i == except || i == exceptToo) continue;
        if (locatePoint(point, polygons_[i]) == PointLocation::Inside) return true;
    }
    return false;
}

bool NavigationMap::contains(Vec2 point) const noexcept
{
    return componentOf(point).has_value();
}

std::optional<std::size_t> NavigationMap::componentOf(Vec2 point) const noexcept
{
    for (std::size_t i = 0; i < polygons_.size(); ++i) {
        const Bounds& b = bounds_[i];
        const auto x = static_cast<double>(point.x);
        const auto y = static_cast<double>(point.y);
        if (x < b.minX - geometryTolerance || x > b.maxX + geometryTolerance || y < b.minY - geometryTolerance ||
            y > b.maxY + geometryTolerance) {
            continue;
        }
        if (locatePoint(point, polygons_[i]) != PointLocation::Outside) return polygonComponent_[i];
    }
    return std::nullopt;
}

bool NavigationMap::isSegmentWalkable(Vec2 from, Vec2 to) const
{
    if (!contains(from) || !contains(to)) return false;
    const double length = distance(from, to);
    if (length <= geometryTolerance) return true;
    // Containment can change only where the segment meets a polygon edge, so it is constant on
    // the pieces between those points: testing each piece's midpoint is exact.
    std::vector<double> cuts{0.0, 1.0};
    const auto minX = static_cast<double>(std::min(from.x, to.x));
    const auto maxX = static_cast<double>(std::max(from.x, to.x));
    const auto minY = static_cast<double>(std::min(from.y, to.y));
    const auto maxY = static_cast<double>(std::max(from.y, to.y));
    for (std::size_t i = 0; i < polygons_.size(); ++i) {
        const Bounds& b = bounds_[i];
        if (!overlaps(minX, maxX, b.minX, b.maxX) || !overlaps(minY, maxY, b.minY, b.maxY)) continue;
        const auto& polygon = polygons_[i];
        for (std::size_t e = 0; e < polygon.size(); ++e) {
            const SegmentIntersection meet = intersectSegments(from, to, polygon[e], polygon[(e + 1) % polygon.size()]);
            if (meet.kind == SegmentIntersection::Kind::None) continue;
            cuts.push_back(meet.t);
            if (meet.kind == SegmentIntersection::Kind::Overlap) cuts.push_back(meet.tEnd);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        if ((cuts[i] - cuts[i - 1]) * length <= geometryTolerance) continue;
        if (!contains(lerp(from, to, (cuts[i - 1] + cuts[i]) / 2.0))) return false;
    }
    return true;
}

Result<NavigationPath> NavigationMap::findPath(Vec2 from, Vec2 to) const
{
    const auto start = componentOf(from);
    if (!start) return makeError(ErrorCode::NotFound, concat("the start ", describePoint(from), " is not walkable"));
    const auto goal = componentOf(to);
    if (!goal) return makeError(ErrorCode::NotFound, concat("the destination ", describePoint(to), " is not walkable"));
    if (*start != *goal) {
        return makeError(ErrorCode::NotFound, concat("the destination ", describePoint(to),
                                                     " cannot be reached from ", describePoint(from),
                                                     " (another walkable area)"));
    }
    if (distance(from, to) <= geometryTolerance) return NavigationPath{{from}, 0.0};
    if (isSegmentWalkable(from, to)) return NavigationPath{{from, to}, distance(from, to)};

    // A* over the graph nodes plus two query nodes: startNode = n, goalNode = n + 1.
    const std::size_t n = nodes_.size();
    const std::size_t startNode = n;
    const std::size_t goalNode = n + 1;
    std::vector<char> seesGoal(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        seesGoal[i] = nodes_[i].component == *goal && isSegmentWalkable(nodes_[i].point, to) ? 1 : 0;
    }
    const auto pointOf = [&](std::size_t node) { return node == startNode ? from : node == goalNode ? to : nodes_[node].point; };
    constexpr double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> cost(n + 2, infinity);
    std::vector<std::size_t> previous(n + 2, n + 2);
    std::vector<char> done(n + 2, 0);
    using Entry = std::pair<double, std::size_t>; // (estimate, node): ties go to the lower index.
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    cost[startNode] = 0.0;
    open.emplace(distance(from, to), startNode);

    const auto relax = [&](std::size_t node, std::size_t next) {
        const double candidate = cost[node] + distance(pointOf(node), pointOf(next));
        if (candidate < cost[next]) {
            cost[next] = candidate;
            previous[next] = node;
            open.emplace(candidate + distance(pointOf(next), to), next);
        }
    };
    while (!open.empty()) {
        const std::size_t node = open.top().second;
        open.pop();
        if (done[node]) continue;
        done[node] = 1;
        if (node == goalNode) break;
        if (node == startNode) {
            for (std::size_t i = 0; i < n; ++i) {
                if (nodes_[i].component == *start && isSegmentWalkable(from, nodes_[i].point)) relax(node, i);
            }
            continue;
        }
        for (const std::size_t next : adjacency_[node]) relax(node, next);
        if (seesGoal[node]) relax(node, goalNode);
    }
    if (!done[goalNode]) {
        return makeError(ErrorCode::NotFound, concat("no path from ", describePoint(from), " to ", describePoint(to)));
    }
    std::vector<Vec2> waypoints;
    for (std::size_t node = goalNode; node != n + 2; node = previous[node]) waypoints.push_back(pointOf(node));
    std::reverse(waypoints.begin(), waypoints.end());
    waypoints = simplify(std::move(waypoints));
    double length = 0.0;
    for (std::size_t i = 1; i < waypoints.size(); ++i) length += distance(waypoints[i - 1], waypoints[i]);
    return NavigationPath{std::move(waypoints), length};
}

std::vector<Vec2> NavigationMap::simplify(std::vector<Vec2> waypoints) const
{
    // Drop duplicates, then skip every waypoint the path can go past in a straight walkable line.
    std::vector<Vec2> unique;
    for (const Vec2 p : waypoints) {
        if (unique.empty() || distance(unique.back(), p) > geometryTolerance) unique.push_back(p);
    }
    if (unique.size() <= 2) return unique;
    std::vector<Vec2> result{unique.front()};
    std::size_t at = 0;
    while (at + 1 < unique.size()) {
        std::size_t next = at + 1;
        for (std::size_t candidate = unique.size() - 1; candidate > at + 1; --candidate) {
            if (isSegmentWalkable(unique[at], unique[candidate])) {
                next = candidate;
                break;
            }
        }
        result.push_back(unique[next]);
        at = next;
    }
    return result;
}

Vec2 NavigationMap::closestPoint(Vec2 point, std::size_t component) const
{
    if (componentOf(point) == component) return point;
    Vec2 best = point;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < polygons_.size(); ++i) {
        if (polygonComponent_[i] != component) continue;
        const auto& polygon = polygons_[i];
        for (std::size_t e = 0; e < polygon.size(); ++e) {
            const Vec2 candidate = closestPointOnSegment(point, polygon[e], polygon[(e + 1) % polygon.size()]).point;
            const double d = distance(point, candidate);
            if (d < bestDistance) {
                bestDistance = d;
                best = candidate;
            }
        }
    }
    return best;
}

Result<Vec2> NavigationMap::resolveDestination(Vec2 from, Vec2 requested, DestinationPolicy policy) const
{
    const auto start = componentOf(from);
    if (!start) return makeError(ErrorCode::NotFound, concat("the start ", describePoint(from), " is not walkable"));
    if (policy == DestinationPolicy::NearestReachable) return closestPoint(requested, *start);
    const auto goal = componentOf(requested);
    if (!goal) {
        return makeError(ErrorCode::NotFound, concat("the destination ", describePoint(requested), " is not walkable"));
    }
    if (*goal != *start) {
        return makeError(ErrorCode::NotFound, concat("the destination ", describePoint(requested),
                                                     " cannot be reached from ", describePoint(from),
                                                     " (another walkable area)"));
    }
    return requested;
}

} // namespace schuimm::navigation
