# core_net: Qt Core/Network-only helpers.
#
# Deliberately depends on NOTHING but Qt::Core and Qt::Network. It exists so
# core_services can do blocking/async HTTP without linking VTextEdit (which
# links Qt::Widgets PUBLIC and is a shared library). Do not add Qt::Gui,
# Qt::Widgets, vxcore or VTextEdit here.
set(NET_SOURCES
  networkutils.cpp
)

set(NET_HEADERS
  networkutils.h
)

add_library(core_net STATIC ${NET_SOURCES} ${NET_HEADERS})

target_link_libraries(core_net
  PUBLIC
    Qt${QT_DEFAULT_MAJOR_VERSION}::Core
    Qt${QT_DEFAULT_MAJOR_VERSION}::Network
)

target_include_directories(core_net
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(core_net PROPERTIES
  AUTOMOC ON
)
