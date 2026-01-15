cmake_minimum_required(VERSION 3.28)

project(MyProject LANGUAGES CXX)

option(YLT_ENABLE_IO_URING "Enable io_uring for yalantinglibs" OFF)
option(RESERVATION_SINGLETHREAD_ENABLED "Enable single-threaded reservation" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.0/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME opencv
    URL https://github.com/opencv/opencv/archive/refs/tags/4.13.0.tar.gz
    OPTIONS
        "BUILD_LIST core,imgproc,imgcodecs"
        "BUILD_opencv_apps OFF"
        "BUILD_opencv_java OFF"
        "BUILD_opencv_python3 OFF"
        "BUILD_opencv_python2 OFF"
        "BUILD_DOCS OFF"
        "BUILD_TESTS OFF"
        "BUILD_PERF_TESTS OFF"
        "BUILD_EXAMPLES OFF"
        "BUILD_SHARED_LIBS OFF"
        "WITH_FFMPEG OFF"
        "WITH_GSTREAMER OFF"
        "WITH_MSMF OFF"
        "WITH_DSHOW OFF"
        "WITH_AVFOUNDATION OFF"
        "WITH_1394 OFF"
        "WITH_OPENCL OFF"
        "WITH_CUDA OFF"
        "WITH_VTK OFF"
        "WITH_GTK OFF"
        "WITH_WIN32UI OFF"
        "WITH_QT OFF"
        "WITH_IPP OFF"
        "WITH_ITT OFF"
        "WITH_QUIRC OFF"
        "WITH_ADE OFF"
        "WITH_PROTOBUF OFF"
        "WITH_TIFF OFF"
        "WITH_JPEG OFF"
        "WITH_JASPER OFF"
        "WITH_OPENJPEG OFF"
        "WITH_OPENEXR OFF"
        "WITH_WEBP OFF"
        "WITH_IMGCODEC_HDR OFF"
        "WITH_IMGCODEC_SUNRASTER OFF"
        "WITH_IMGCODEC_PXM OFF"
        "WITH_IMGCODEC_PFM OFF"
        "WITH_PNG ON"
        "BUILD_PNG ON"
        "BUILD_ZLIB ON"
        "BUILD_WITH_STATIC_CRT OFF"
        "CPU_BASELINE AVX2"
        "OPENCV_GENERATE_SETUPVARS OFF"
        "OPENCV_FORCE_3RDPARTY_BUILD ON"
        "CV_TRACE OFF"
)

CPMAddPackage(
    NAME openssl-cmake
    GIT_REPOSITORY https://github.com/jimmy-park/openssl-cmake.git
    GIT_TAG 3.6.0
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
)

CPMAddPackage(
    NAME yalantinglibs
    URL https://github.com/alibaba/yalantinglibs/archive/refs/tags/0.5.7.zip
    OPTIONS 
        "BUILD_EXAMPLES OFF"
        "BUILD_BENCHMARKS OFF"
        "YLT_ENABLE_SSL ON"    
        "YLT_ENABLE_IO_URING ${YLT_ENABLE_IO_URING}"
)

CPMAddPackage(
    NAME json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

CPMAddPackage(
    NAME fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0
)

CPMAddPackage(
    NAME yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
    OPTIONS
        "YAML_BUILD_SHARED_LIBS OFF"
        "YAML_CPP_BUILD_TOOLS OFF"
        "CMAKE_POLICY_VERSION_MINIMUM 3.5"
)

CPMAddPackage(
    NAME simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v4.2.4
    OPTIONS
        "SIMDJSON_DISABLE_DEPRECATED_API ON"
)

if(MSVC)
    add_compile_options(/bigobj)
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wa,-mbig-obj)
endif()

add_compile_definitions(FMT_HEADER_ONLY=0)

if(RESERVATION_SINGLETHREAD_ENABLED)
    set_source_files_properties(src/network/api_client.cpp PROPERTIES COMPILE_DEFINITIONS RESERVATION_SINGLETHREAD_ENABLED)
endif()

add_library(BotCore STATIC
    src/config/config.cpp
    src/network/api_client.cpp
    src/utils/crypto.cpp
    src/captcha/captcha.cpp
    src/reservation/logic.cpp
)

target_include_directories(BotCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

foreach(module core imgcodecs imgproc)
    list(APPEND OpenCV_INCLUDE_DIRS ${opencv_SOURCE_DIR}/modules/${module}/include)
    list(APPEND OpenCV_LIBS opencv_${module})
endforeach()

list(APPEND OpenCV_INCLUDE_DIRS 
    ${OpenCV_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(BotCore PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    OpenSSL::applink
    yalantinglibs::yalantinglibs
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    fmt::fmt
    ${OpenCV_LIBS}
    simdjson::simdjson
    $<$<PLATFORM_ID:Windows>:dbghelp>
)
target_include_directories(BotCore PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)

add_executable(${PROJECT_NAME} src/main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE 
)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    BotCore
    yalantinglibs::yalantinglibs
)

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
elseif(WIN32 AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(${PROJECT_NAME} PRIVATE 
        $<$<CONFIG:Release>:-static -s>
        $<$<CONFIG:RelWithDebInfo>:-static>
    )
else()
    target_link_options(${PROJECT_NAME} PRIVATE 
        $<$<CONFIG:Release>:-static-libstdc++ -static-libgcc -s>
        $<$<CONFIG:RelWithDebInfo>:-static-libstdc++ -static-libgcc>
    )
endif()

enable_testing()

add_executable(network_benchmark tests/network_benchmark.cpp)
target_link_libraries(network_benchmark PRIVATE BotCore yalantinglibs::yalantinglibs)

if(MSVC)
    target_link_options(network_benchmark PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
elseif(WIN32 AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(network_benchmark PRIVATE 
        $<$<CONFIG:Release>:-static -s>
        $<$<CONFIG:RelWithDebInfo>:-static>
    )
else()
    target_link_options(network_benchmark PRIVATE 
        $<$<CONFIG:Release>:-static-libstdc++ -static-libgcc -s>
        $<$<CONFIG:RelWithDebInfo>:-static-libstdc++ -static-libgcc>
    )
endif()

add_test(NAME NetworkBenchmark COMMAND network_benchmark)

add_executable(network_profiling tests/network_profiling.cpp src/network/api_client_profiling.cpp)
target_link_libraries(network_profiling PRIVATE BotCore yalantinglibs::yalantinglibs)

if(MSVC)
    target_link_options(network_profiling PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
elseif(WIN32 AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(network_profiling PRIVATE
        $<$<CONFIG:Release>:-static -s>
        $<$<CONFIG:RelWithDebInfo>:-static>
    )
else()
    target_link_options(network_profiling PRIVATE
        $<$<CONFIG:Release>:-static-libstdc++ -static-libgcc -s>
        $<$<CONFIG:RelWithDebInfo>:-static-libstdc++ -static-libgcc>
    )
endif()

add_test(NAME NetworkProfiling COMMAND network_profiling)

add_executable(network_profiling_local tests/network_profiling_local.cpp src/network/api_client_profiling.cpp)
target_link_libraries(network_profiling_local PRIVATE BotCore yalantinglibs::yalantinglibs)

if(MSVC)
    target_link_options(network_profiling_local PRIVATE $<$<CONFIG:Release>:/DEBUG:NONE>)
elseif(WIN32 AND (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_link_options(network_profiling_local PRIVATE
        $<$<CONFIG:Release>:-static -s>
        $<$<CONFIG:RelWithDebInfo>:-static>
    )
else()
    target_link_options(network_profiling_local PRIVATE
        $<$<CONFIG:Release>:-static-libstdc++ -static-libgcc -s>
        $<$<CONFIG:RelWithDebInfo>:-static-libstdc++ -static-libgcc>
    )
endif()

add_test(NAME NetworkProfilingLocal COMMAND network_profiling_local)

