add_qt_test(test_apiauth SOURCES test_apiauth.cpp GUILESS LINKS vnote_api)
add_qt_test(test_restapi SOURCES test_restapi.cpp GUILESS LINKS vnote_api Qt6::Network)
add_qt_test(test_vnoteapibackend SOURCES test_vnoteapibackend.cpp GUILESS
  LINKS vnote_httpapi core_services core_configs Qt6::Core5Compat)
add_qt_test(test_httpapilifecycle SOURCES test_httpapilifecycle.cpp GUILESS
  LINKS vnote_httpapi core_services core_configs Qt6::Network)
set_tests_properties(test_restapi PROPERTIES TIMEOUT 120)
