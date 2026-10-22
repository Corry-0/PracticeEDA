cmake_minimum_required(VERSION 3.20)
project(PracticeErcAudit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(PRACTICEEDA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(PRACTICEEDA_BUILD_DIR "${PRACTICEEDA_SOURCE_DIR}/build" CACHE PATH "Existing PracticeEDA build directory")
add_library(audited_logic_core STATIC IMPORTED)
set_target_properties(audited_logic_core PROPERTIES
    IMPORTED_CONFIGURATIONS "Release;Debug"
    IMPORTED_LOCATION_RELEASE "${PRACTICEEDA_BUILD_DIR}/Release/logic_core.lib"
    IMPORTED_LOCATION_DEBUG "${PRACTICEEDA_BUILD_DIR}/Debug/logic_core.lib")
add_executable(erc_audit erc_audit.cpp)
target_include_directories(erc_audit PRIVATE "${PRACTICEEDA_SOURCE_DIR}/include")
target_include_directories(erc_audit SYSTEM PRIVATE "${PRACTICEEDA_SOURCE_DIR}/third_party")
target_compile_definitions(erc_audit PRIVATE NOMINMAX)
target_compile_options(erc_audit PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(erc_audit PRIVATE audited_logic_core)
