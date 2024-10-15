# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(pks)

set(CMAKE_CXX_STANDARD 20)

# recursively find all .cpp files, excluding 'cmake-build-debug':
file(GLOB_RECURSE SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*cmake-build-debug.*")

message(STATUS "Adding source files:")
foreach(SOURCE ${SOURCES})
    message(STATUS "  ${SOURCE}")
endforeach()

# creating executable
message(STATUS "Creating executable: p2p")
add_executable(p2p ${SOURCES})

# all libraries linking:

# readline library
find_library(READLINE_LIBRARY NAMES readline)

if (NOT READLINE_LIBRARY)
    message(FATAL_ERROR "readline library not found, use sudo apt-get install libreadline-dev")
else()
    target_link_libraries(p2p PRIVATE ${READLINE_LIBRARY})
endif()

# pthread library
find_library(PTHREAD_LIBRARY NAMES pthread)

if (NOT PTHREAD_LIBRARY)
    message(FATAL_ERROR "pthread library not found")
else()
    target_link_libraries(p2p PRIVATE ${PTHREAD_LIBRARY})
endif()

# spdlog library
find_package(spdlog REQUIRED)

if (NOT spdlog_FOUND)
    message(FATAL_ERROR "spdlog library not found")
else()
    target_link_libraries(p2p PRIVATE spdlog::spdlog)
endif()

message(STATUS "Linking libraries:")
message(STATUS "  readline: ${READLINE_LIBRARY}")
message(STATUS "  pthread: ${PTHREAD_LIBRARY}")
message(STATUS "  spdlog: ${spdlog_DIR}")

# add COMMIT_HASH macro
execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE COMMIT_HASH
        ERROR_VARIABLE GIT_ERROR
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions(-DCOMMIT_HASH=\"${COMMIT_HASH}\")