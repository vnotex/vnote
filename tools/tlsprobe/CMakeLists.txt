# tlsprobe is a CI gate executable, not part of the shipped product.
#
# Qt must be discovered HERE rather than relying on the find_package() in
# src/CMakeLists.txt: imported targets such as Qt5::Core / Qt5::Network are
# directory-scoped unless created GLOBAL, so this sibling directory (added from
# the top level) cannot see targets found under src/.
find_package(Qt${QT_DEFAULT_MAJOR_VERSION} REQUIRED COMPONENTS Core Network)

add_executable(tlsprobe main.cpp)

target_link_libraries(tlsprobe PRIVATE
    Qt${QT_DEFAULT_MAJOR_VERSION}::Core
    Qt${QT_DEFAULT_MAJOR_VERSION}::Network)

# NO install() rule on purpose: this must never enter the package. The
# "no stray executable" gate in .github/workflows/ci-win.yml is the backstop.
