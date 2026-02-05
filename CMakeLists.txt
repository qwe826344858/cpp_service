cmake_minimum_required(VERSION 3.10)
project(vad_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 寻找依赖
# 提示: 请使用 vcpkg 安装 websocketpp 和 boost-asio
# vcpkg install websocketpp boost-asio boost-system boost-thread

find_package(websocketpp CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(include)

add_executable(vad_server 
    src/main.cpp 
    src/server.cpp 
    src/session.cpp
)

target_link_libraries(vad_server PRIVATE websocketpp::websocketpp Boost::system Boost::thread)

# Windows Socket 库
if(WIN32)
    target_link_libraries(vad_server PRIVATE ws2_32 mswsock)
endif()
