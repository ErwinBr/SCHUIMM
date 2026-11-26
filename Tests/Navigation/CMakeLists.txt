# Geometry predicates, polygon validation and navigation (walkable queries, destination
# resolution, A* pathfinding). Headless.
schuimm_add_test(NavigationTests
    SOURCES NavigationTests.cpp
    LIBRARIES Schuimm::Navigation
    LABELS unit)
