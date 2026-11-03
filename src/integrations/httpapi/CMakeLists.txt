add_library(vnote_httpapi STATIC
  apicredentialstore.cpp apicredentialstore.h
  httpapicontroller.cpp httpapicontroller.h
  vnoteapibackend.cpp vnoteapibackend.h
)

target_compile_features(vnote_httpapi PUBLIC cxx_std_17)
target_include_directories(vnote_httpapi PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(vnote_httpapi
  PUBLIC vnote_api Qt6::Core
  PRIVATE core_services Qt6::Concurrent Qt6::Core5Compat
)
if(WIN32)
  target_link_libraries(vnote_httpapi PRIVATE advapi32)
endif()
set_target_properties(vnote_httpapi PROPERTIES AUTOMOC ON)
