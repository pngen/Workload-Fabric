cmake_minimum_required(VERSION 3.24)

project(WorkloadFabric
    VERSION 1.0.0
    DESCRIPTION "Open-source, vendor-neutral C++20 runtime for governing long-running AI workload lifecycle"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WorkloadFabric_BUILD_TESTING "Build tests" ON)
option(WorkloadFabric_BUILD_BENCHMARKS "Build benchmarks" ON)
option(WorkloadFabric_BUILD_EXAMPLES "Build examples" ON)
option(WorkloadFabric_BUILD_TOOLS "Build CLI inspection tool" ON)
option(WorkloadFabric_BUILD_CUDA "Build CUDA-backed proofs (requires CUDA)" OFF)
option(WorkloadFabric_SANITIZE "Build with AddressSanitizer (MSVC/GCC/Clang, where supported)" OFF)

if(WorkloadFabric_SANITIZE AND MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address>)
elseif(WorkloadFabric_SANITIZE)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address> $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
endif()

if(MSVC)
  # Scope strict flags to CXX sources only; nvcc (CUDA) must not receive them.
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX> $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
  add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:_CRT_SECURE_NO_WARNINGS>)
  add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra> $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic> $<$<COMPILE_LANGUAGE:CXX>:-Wconversion> $<$<COMPILE_LANGUAGE:CXX>:-Wshadow> $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
endif()

# ---- Core library ------------------------------------------------------
file(GLOB_RECURSE WF_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE WF_PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")

add_library(WorkloadFabric ${WF_SOURCES})
add_library(WorkloadFabric::WorkloadFabric ALIAS WorkloadFabric)
target_include_directories(WorkloadFabric PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Public compile features (C++20) is carried via target_compile_features.
target_compile_features(WorkloadFabric PUBLIC cxx_std_20)

if(WIN32)
  # Frame the loopback transport on Winsock; the reference transport is Winsock.
  target_link_libraries(WorkloadFabric PUBLIC ws2_32)
endif()

set_target_properties(WorkloadFabric PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# Order matters: tests reference the wf_worker target defined in tools, and need the
# worker + coordinator utilities, so tools/examples are added before tests.
if(WorkloadFabric_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(WorkloadFabric_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(WorkloadFabric_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(WorkloadFabric_BUILD_CUDA)
  # CUDA language requires a Ninja generator on Windows (the Visual Studio CUDA
  # toolset is not reliably present). Build with:
  #   tools\wf_env.cmd cmake -G Ninja -DWorkloadFabric_BUILD_CUDA=ON ...
  set(CMAKE_CUDA_ARCHITECTURES 120)  # sm_120 (RTX 5090); must be set before enable_language(CUDA)
  enable_language(CUDA)
  add_subdirectory(cuda)
endif()
if(WorkloadFabric_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Install & package ------------------------------------------------------
install(TARGETS WorkloadFabric EXPORT WorkloadFabricTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT WorkloadFabricTargets
    FILE WorkloadFabricTargets.cmake
    NAMESPACE WorkloadFabric::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WorkloadFabric)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WorkloadFabricConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WorkloadFabricConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WorkloadFabric)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WorkloadFabricConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WorkloadFabricConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WorkloadFabricConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WorkloadFabric)

