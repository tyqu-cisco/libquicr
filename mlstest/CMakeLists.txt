set(TEST_APP_NAME "mlstest")

# Dependencies
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
find_package(doctest REQUIRED)
find_package(OpenSSL 1.1 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(MLSPP REQUIRED)

# Test Binary
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${TEST_APP_NAME} ${TEST_SOURCES})
add_dependencies(${TEST_APP_NAME} quicr)
target_link_libraries( ${TEST_APP_NAME} 
  LINK_PUBLIC
  quicr 
  MLSPP::mlspp 
  doctest::doctest)

# Make the compiler stricter
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${TEST_APP_NAME} PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
elseif(MSVC)
  target_compile_options(${TEST_APP_NAME} PRIVATE /W4 /WX)
endif()

# Use C++20
set_target_properties(${TEST_APP_NAME} 
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

# Enable CTest
include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(${TEST_APP_NAME} ADD_LABELS 0)

