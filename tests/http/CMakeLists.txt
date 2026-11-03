find_package(Qt6 REQUIRED COMPONENTS Network)
find_package(Threads REQUIRED)

add_qt_test(test_httpserver
  SOURCES test_httpserver.cpp
  LINKS vnote_http Qt6::Network Threads::Threads
  GUILESS
)
