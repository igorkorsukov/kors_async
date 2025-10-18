cmake_minimum_required(VERSION 3.5)

project(async_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)


include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY git@github.com:google/googletest.git
  GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(googletest)

include(${CMAKE_CURRENT_LIST_DIR}/../async/async.cmake)

add_executable(${PROJECT_NAME}
    ${KORS_ASYNC_SRC}
    channel_tests.cpp
    ringqueue_tests.cpp
    rpcqueue_tests.cpp
)
target_link_libraries(${PROJECT_NAME} gtest_main)
