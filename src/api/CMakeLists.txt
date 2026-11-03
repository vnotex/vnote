add_library(vnote_api STATIC
  apiserver.cpp apiserver.h apiauth.cpp apiauth.h apioperations.cpp apioperations.h
  apitypes.h iapibackend.h iapicredentialstore.h apiaudit.cpp apiaudit.h
)
target_compile_features(vnote_api PUBLIC cxx_std_17)
target_include_directories(vnote_api PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vnote_api PUBLIC vnote_http Qt6::Core)
set_target_properties(vnote_api PROPERTIES AUTOMOC ON)
