# Navigation: walkable areas and pathfinding

`Source/Navigation` (library `schuimm_navigation`) answers three kinds of question about a room's
walkable areas:
- whether a point or a straight segment is walkable;
- where a walk towards a point should end;
- the shortest path between two walkable points.

It depends on Core only. It knows nothing about actors, animation, rendering, input or scripts: it
takes and returns room coordinates. Actor movement ([Actors.md](Actors.md#movement)) uses it.
Geometry predicates are in `Core/Common/GeometryPredicates.h`.

## Walkable areas

Walkable areas are polygons in the room resource (`navigation.walkable_areas`, [Rooms.md](Rooms.md)).
They describe where an actor's **ground point** may be. Sprite sizes play no part: an actor is a
point for navigation, and actors may overlap one another.

```json
"navigation": {
    "walkable_areas": [
        { "id": "floor_north", "points": [[40, 300], [600, 300], [600, 240], [920, 240], [920, 380], [40, 380]] },
        { "id": "floor_west", "points": [[40, 370], [430, 370], [430, 460], [40, 460]] },
        { "id": "balcony", "points": [[700, 120], [900, 120], [900, 190], [700, 190]] }
    ]
}
```

**The walkable domain** is the closed union of all walkable polygons.
- **Boundaries:** a point on a polygon's boundary is walkable.
- **Overlap:** polygons may overlap, touch or share edges. An edge that another polygon covers is
  not an obstacle, so only the boundary of the union limits movement.
- **Obstacles:** non-walkable space, including interior islands, is whatever no polygon covers.
  Arrange polygons around an obstacle to make an island. The courtyard in ActorDemo surrounds its
  fountain with four overlapping rectangles. There is no separate obstacle format.
- **Components:** polygons that overlap or touch form one connected component. Nothing is
  reachable across components; the ActorDemo balcony is one.

**Rules**, checked when the room is loaded:
- each polygon is valid (below);
- all its points lie inside the room, `[0, width] × [0, height]`;
- a room has at most 32 walkable areas and 256 walkable-area points in total.

The limits keep building the navigation map cheap. Regions (`regions`) may lie partly outside the
room, but must be valid polygons too.

### Polygon validation (`Navigation/Polygon.h`)

A valid polygon is simple:

- 3 to 128 points, all finite;
- no two points within the tolerance (1/64 px) of each other. This covers repeated points, zero-length
  edges, and a last point that repeats the first (polygons close implicitly);
- an area of at least 10⁻⁶ square pixels;
- adjacent edges meet only at their shared point; they do not fold back over each other;
- non-adjacent edges do not intersect, touch or overlap.

**Accepted:** either winding order, concave polygons, and collinear points that don't fold back.
The authored points are kept as they are.

**How it is checked:** on the coordinates as stored (float), not on the JSON numbers. Edges are
compared pairwise, which is O(n²).

**Errors** name the offending points or edges, for example:
```text
InvalidData: Rooms/Courtyard.room.json: navigation.walkable_areas[0].points: edges 1 (points[1]-points[2]) and 4 (points[4]-points[0]) intersect
```

## Numerical tolerance

The tolerance is one distance: `geometryTolerance`, 1/64 room pixel. It decides whether two points
are the same, and whether a point lies on a segment or a line.

- **Orientation tests are distances.** `orientation(a, b, c)` returns the signed distance of `c`
  from line `ab`, not a raw cross product. So the same tolerance means the same thing for long
  and short edges.
- **Double precision throughout.** All predicates compute in double, including the differences of
  coordinates. A difference of two floats taken as a float would round before any double
  computation began.
- **Why 1/64 px.** Points computed in double are stored as floats: edge meeting points, nearest
  points, and positions along a path. At room coordinates up to 65536, neighbouring floats are up
  to 1/128 px apart, so a stored point can be 1/256 px off the exact one. An actor that walked to
  a boundary point stands within that distance of the boundary and must still count as standing
  on it. 1/64 px is four times the worst rounding, exact in binary, and far below a visible pixel.
  Authored points must therefore be more than 1/64 px apart.
- **Other thresholds:** areas have their own (above). Facing sectors are exact and need none.

## The navigation map (`Navigation/NavigationMap.h`)

`NavigationMap::create(polygons)` builds an immutable map. `RoomLibrary` builds one per room
resource and caches it, so every instance of the room shares it (`LoadedRoom::navigation`, which is
null without walkable areas).

**Size limit:** the corner nodes, which are authored points plus meeting points, are limited to
512 (`maxNavigationNodes`), because overlapping polygons can meet in many more places than they
have points. A room whose walkable areas produce more fails to load, quickly: collecting stops
early. The error reads "the walkable areas produce more than 512 navigation corners (polygons
overlap in too many places)". A map with 480 corners builds in about 1.2 s in a Debug build.

**Precomputed:**
- **Components:** found by union-find over pairs of polygons that intersect or touch, or where one
  contains the other.
- **Corner nodes:** candidates for the corners of the union's boundary:
  - polygon points that are not strictly inside another polygon;
  - the points where edges of different polygons meet, which are not strictly inside a third
    polygon. Where polygons overlap, the union's corners are often such meeting points rather than
    authored points.
  - Nodes are de-duplicated within the tolerance, in a fixed order.
- **Visibility graph:** an edge between every two nodes of one component whose connecting segment
  is walkable.

**Queries:**
- `contains(p)`: whether `p` is walkable.
- `componentOf(p)`: which component `p` is in.
- `isSegmentWalkable(a, b)`: true if the whole segment lies in the domain.
  1. Both ends must be walkable.
  2. The segment is cut wherever it meets a polygon edge, and the midpoint of every piece is tested.
  3. The domain's boundary lies on polygon edges, so containment cannot change inside a piece, and
     the test is exact.

  Testing only the endpoints, or the midpoint, would accept a segment that crosses a notch.
- `findPath(from, to)`, which is strict:
  - **Errors:** `NotFound` if either point is not walkable or they lie in different components.
  - **Same point:** a one-point path.
  - **Direct:** the segment itself when it is walkable.
  - **Otherwise A\*:** over the visibility graph, with `from` and `to` as temporary nodes linked to
    the nodes they can see. Cost and heuristic are both Euclidean distance. Speed and perspective
    play no part: the path is geometrically shortest.
  - **Determinism:** queue ties go to the lower node index, so equal-cost routes always come out
    the same.
  - **Simplification:** consecutive duplicates are removed, and a waypoint is skipped whenever the
    segment past it is walkable, so collinear corners never become waypoints.
  - **Guarantee:** every returned segment is walkable.
- `closestPoint(p, component)`: `p` itself if it is in that component. Otherwise, the nearest point
  on the component's polygon edges, with ties going to the lower polygon index, then edge index.
- `resolveDestination(from, requested, policy)`:
  - `Strict`: `requested` must be walkable and in `from`'s component.
  - `NearestReachable`: the closest point of `from`'s component, never a nearer point of a
    disconnected area. Click-to-walk uses this.
  - Both fail if `from` itself is not walkable.

**For debugging:** `polygons()`, `nodes()` and `edges()` expose the data read-only. ActorDemo
draws them with G; drawing is not part of navigation.

### Complexity

With V nodes (at most the walkable points plus their meeting points) and E polygon edges:

| Operation | Cost |
|---|---|
| Building the map | O(V²·E) segment tests (visibility graph), plus O(E²) for components and meeting points; V ≤ 512, E ≤ 256 |
| Segment test | O(E log E) |
| A path query | O(V·E) to link the start and goal, then A* over at most V² edges: O(V² log V) |
| Nearest reachable point | O(E) |

The limits (256 authored points, 512 corner nodes) keep this fine for interactive use.

**Practical limitations:**
- there is no clearance for actor size;
- there are no dynamic obstacles, navigation links or one-way areas;
- the cost is distance, not travel time.

## Tests

`Tests/Navigation/NavigationTests.cpp` covers:
- vector operations, orientation, closest points, segment intersection (crossings, touches,
  overlaps, tolerance), point in polygon;
- every polygon defect, including a self-intersecting polygon with non-zero area;
- walkable queries, direct paths, concave boundaries, islands, overlapping polygons and their
  meeting points, shared edges, disconnected areas, boundary points, points outside the domain,
  nearest-reachable resolution, deterministic ties and simplification;
- re-checking every returned segment against `isSegmentWalkable`.
