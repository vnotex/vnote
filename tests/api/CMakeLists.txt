add_qt_test(test_apiauth SOURCES test_apiauth.cpp GUILESS LINKS vnote_api)
add_qt_test(test_restapi SOURCES test_restapi.cpp GUILESS LINKS vnote_api Qt6::Network)
add_qt_test(test_vnoteapibackend SOURCES test_vnoteapibackend.cpp GUILESS
  LINKS vnote_httpapi core_services core_configs Qt6::Core5Compat)
set(_httpapilifecycle_report_args)
if(WIN32)
  # Preserve first-run assertions even when hosted console capture omits them.
  set(_httpapilifecycle_report_args
    -o -,txt
    -o test_httpapilifecycle.txt,txt
    -o test_httpapilifecycle.xml,junitxml
  )
endif()
add_qt_test(test_httpapilifecycle SOURCES test_httpapilifecycle.cpp GUILESS
  LINKS vnote_httpapi core_services core_configs Qt6::Network
  ARGS ${_httpapilifecycle_report_args})
set_tests_properties(test_restapi PROPERTIES TIMEOUT 120)
