cmake_minimum_required(VERSION 3.10)
project(cookrpc VERSION 1.0)

# 确保使用 vcpkg
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")

    # 添加编译定义
    add_definitions(-DTHREADED)
    add_definitions(-DZK_DEPRECATED=1)
endif()

# 使用 vcpkg 查找所有依赖
find_package(Protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ZooKeeper C client:
# vcpkg 的 zookeeper 包可能只提供单线程库，缺少同步 API（zoo_create/zoo_exists 等）。
# 这种情况下通过下面的变量显式指定一个带 THREADED 的 libzookeeper_mt。
set(RPC_ZOOKEEPER_MT_LIB "" CACHE FILEPATH "Path to libzookeeper_mt.a (threaded C client)")
set(RPC_ZOOKEEPER_HASHTABLE_LIB "" CACHE FILEPATH "Path to libzookeeper_hashtable.a")
if(RPC_ZOOKEEPER_MT_LIB)
    set(RPC_ZOOKEEPER_LIBS ${RPC_ZOOKEEPER_MT_LIB} ${RPC_ZOOKEEPER_HASHTABLE_LIB})
    find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h)
else()
    find_package(unofficial-zookeeper CONFIG REQUIRED)
    set(RPC_ZOOKEEPER_LIBS unofficial::zookeeper::zookeeper)
    set(ZOOKEEPER_INCLUDE_DIR "")
endif()

# 设置源代码目录
set(RPC_SOURCE_DIR ${CMAKE_SOURCE_DIR}/rpc_src)

# 收集公共源文件（排除 core 目录，因为它包含 main 文件）
file(GLOB_RECURSE COMMON_SOURCES
    "${RPC_SOURCE_DIR}/compress_data/*.cpp"
    "${RPC_SOURCE_DIR}/conn_balancer/*.cpp"
    "${RPC_SOURCE_DIR}/encrypt/*.cpp"
    "${RPC_SOURCE_DIR}/load_config/*.cpp"
    "${RPC_SOURCE_DIR}/network/*.cpp"
    "${RPC_SOURCE_DIR}/protocol/*.cpp"
    "${RPC_SOURCE_DIR}/registry/*.cpp"
    "${RPC_SOURCE_DIR}/service/*.cpp"
    "${RPC_SOURCE_DIR}/thread_pool/*.cpp"
    "${RPC_SOURCE_DIR}/serializer/*.cpp"
    "${CMAKE_SOURCE_DIR}/utils/*.cpp"
    "${CMAKE_SOURCE_DIR}/protos/*.cc"
)

# 过滤掉测试文件和 demo 文件
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*test.*\\.cpp$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*demo.*\\.cpp$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# 收集客户端和服务器特定源文件
set(CLIENT_SOURCES
    ${RPC_SOURCE_DIR}/core/rpc_client.cpp
    ${RPC_SOURCE_DIR}/core/client_main.cpp
)

set(SERVER_SOURCES
    ${RPC_SOURCE_DIR}/core/servers_main.cpp
)

set(STRESS_TEST_SOURCES
    ${RPC_SOURCE_DIR}/core/rpc_client.cpp
    ${RPC_SOURCE_DIR}/tools/stress_test_client.cpp
)

# 创建可执行文件
add_executable(rpc_server
    ${COMMON_SOURCES}
    ${SERVER_SOURCES}
)

add_executable(rpc_client
    ${COMMON_SOURCES}
    ${CLIENT_SOURCES}
)

add_executable(stress_test_client
    ${COMMON_SOURCES}
    ${STRESS_TEST_SOURCES}
)

# 协议与分帧单元测试
add_executable(rpc_tests
    ${COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/tests/protocol_test.cpp
)

enable_testing()
add_test(NAME protocol_tests COMMAND rpc_tests)

# 设置包含目录和链接库
foreach(target rpc_server rpc_client stress_test_client rpc_tests)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${RPC_SOURCE_DIR}
        ${RPC_SOURCE_DIR}/compress_data
        ${RPC_SOURCE_DIR}/conn_balancer
        ${RPC_SOURCE_DIR}/core
        ${RPC_SOURCE_DIR}/encrypt
        ${RPC_SOURCE_DIR}/load_config
        ${RPC_SOURCE_DIR}/network
        ${RPC_SOURCE_DIR}/protocol
        ${RPC_SOURCE_DIR}/registry
        ${RPC_SOURCE_DIR}/serializer
        ${RPC_SOURCE_DIR}/service
        ${RPC_SOURCE_DIR}/thread_pool
        ${CMAKE_SOURCE_DIR}/utils
        ${CMAKE_SOURCE_DIR}/protos
        ${Protobuf_INCLUDE_DIRS}
        ${ZOOKEEPER_INCLUDE_DIR}
    )

    target_link_libraries(${target} PRIVATE
        protobuf::libprotobuf
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        ${RPC_ZOOKEEPER_LIBS}
        OpenSSL::Crypto
        Threads::Threads
        ${CMAKE_DL_LIBS}
        rt
        m
        $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,zstd::libzstd_static>
    )

    target_compile_options(${target} PRIVATE
        -Wno-deprecated-declarations
        -Wno-unused-parameter
    )

    target_compile_definitions(${target} PRIVATE
        THREADED
        ZK_DEPRECATED=1
    )
endforeach()
