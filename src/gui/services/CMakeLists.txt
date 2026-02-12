# GUI Services
# Provides GUI-aware services for presentation logic
# Added to vnote target directly (same pattern as core/)

target_sources(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/themeservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/themeservice.h
)

target_include_directories(vnote PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
