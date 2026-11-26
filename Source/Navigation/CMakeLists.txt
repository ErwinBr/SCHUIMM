# Navigation over walkable polygons: polygon validation, point and segment queries, destination
# resolution and visibility-graph A* pathfinding. Platform-, graphics- and script-independent:
# depends on Core only (geometry predicates). Documentation/Architecture/Navigation.md.
add_library(schuimm_navigation STATIC
    NavigationMap.cpp
    Polygon.cpp)
add_library(Schuimm::Navigation ALIAS schuimm_navigation)
target_link_libraries(schuimm_navigation PUBLIC Schuimm::Core)
schuimm_configure_target(schuimm_navigation)
