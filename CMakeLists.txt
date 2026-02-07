cmake_minimum_required(VERSION 3.15)
project(vad_service)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

# 查找依赖
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(websocketpp REQUIRED)
find_package(Threads REQUIRED)

# ONNX Runtime setup
# set(ONNXRUNTIME_DIR ${CMAKE_SOURCE_DIR}/onnxruntime)
# include_directories(${ONNXRUNTIME_DIR}/include)
# link_directories(${ONNXRUNTIME_DIR}/lib)

# 包含头文件目录
include_directories(include)

if(WEBSOCKETPP_INCLUDE_DIR)
    include_directories(${WEBSOCKETPP_INCLUDE_DIR})
endif()

include_directories(${Boost_INCLUDE_DIRS})

# 定义可执行文件
add_executable(vad_server 
    src/main.cpp 
    src/server.cpp 
    src/session.cpp
    src/vad_iterator.cpp
    src/sherpa_vad_detector.cpp
)

# 链接依赖库
if(TARGET websocketpp::websocketpp)
    target_link_libraries(vad_server PRIVATE websocketpp::websocketpp)
else()
    target_include_directories(vad_server PRIVATE ${WEBSOCKETPP_INCLUDE_DIR})
endif()

target_link_libraries(vad_server PRIVATE Boost::system Boost::thread Threads::Threads)

# 查找并链接 ONNX Runtime (优先使用系统安装的)
find_library(ONNXRUNTIME_LIB onnxruntime)
if(ONNXRUNTIME_LIB)
    message(STATUS "Found ONNX Runtime: ${ONNXRUNTIME_LIB}")
    target_link_libraries(vad_server PRIVATE ${ONNXRUNTIME_LIB})
else()
    message(FATAL_ERROR "ONNX Runtime library not found!")
endif()

# Test VAD integration (Always build this to verify VAD)
add_executable(test_vad src/test_vad.cpp src/vad_iterator.cpp)
target_link_libraries(test_vad PRIVATE ${ONNXRUNTIME_LIB})
