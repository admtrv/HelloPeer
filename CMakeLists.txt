# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(pks)

set(CMAKE_CXX_STANDARD 20)

# all .cpp files linking:
file(GLOB_RECURSE SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*cmake-build-debug.*")

message(STATUS "Linking source files:")
foreach(SOURCE ${SOURCES})
    message(STATUS "  ${SOURCE}")
endforeach()

# adding executable
message(STATUS "Adding executable: p2p")
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

message(STATUS "Linking libraries:")
message(STATUS "  readline: ${READLINE_LIBRARY}")
message(STATUS "  pthread: ${PTHREAD_LIBRARY}")
