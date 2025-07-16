cmake_minimum_required(VERSION 3.15)

project(EventCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include FetchContent module
include(FetchContent)

# Fetch moodycamel/concurrentqueue
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        v1.0.3
)

# Fetch martinus/robin_hood_map
FetchContent_Declare(
    robin_hood_map
    GIT_REPOSITORY https://github.com/martinus/robin-hood-hashing.git
    GIT_TAG        3.11.5
)

FetchContent_MakeAvailable(concurrentqueue robin_hood_map)

# Compiler-specific optimizations and warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(OPTIMIZATION_FLAGS "-O3 -march=native -flto")
    set(WARNING_FLAGS "-Wall -Wextra -Wpedantic -Wconversion")
    
    # Note: -ffast-math can break IEEE compliance and affect determinism
    # Enable only if you understand the implications for your use case
    if(ENABLE_FAST_MATH)
        set(OPTIMIZATION_FLAGS "${OPTIMIZATION_FLAGS} -ffast-math")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(OPTIMIZATION_FLAGS "/O2 /GL")
    set(WARNING_FLAGS "/W4")
    
    # Note: /fp:fast can affect floating-point determinism
    if(ENABLE_FAST_MATH)
        set(OPTIMIZATION_FLAGS "${OPTIMIZATION_FLAGS} /fp:fast")
    endif()
    
    # Suppress specific warnings from external dependencies
    set(EXTERNAL_WARNING_SUPPRESSIONS 
        "/wd26495"  # Uninitialized member variable (from moodycamel)
        "/wd26819"  # Unannotated fallthrough (from robin_hood)
        "/wd6305"   # Potential sizeof/countof mismatch (from robin_hood)
    )
endif()

# Set build type specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")
endif()

# Add warning flags for all builds
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS}")

# EventCore library
set(EVENTCORE_HEADERS
    include/EventCore/Event.hpp
    include/EventCore/EventId.hpp
    include/EventCore/EventDispatcher.hpp
)

set(EVENTCORE_SOURCES
    src/EventDispatcher.cpp
)

add_library(EventCore STATIC ${EVENTCORE_SOURCES} ${EVENTCORE_HEADERS})

target_include_directories(EventCore 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${concurrentqueue_SOURCE_DIR}
        ${robin_hood_map_SOURCE_DIR}/src/include
)

# Link external dependencies  
target_link_libraries(EventCore 
    PUBLIC 
        robin_hood
    PRIVATE 
        concurrentqueue
)

# Platform-specific threading libraries
find_package(Threads REQUIRED)
target_link_libraries(EventCore PRIVATE Threads::Threads)

# Apply warning suppressions for external dependencies (MSVC only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(EventCore PRIVATE ${EXTERNAL_WARNING_SUPPRESSIONS})
endif()

# Optional: Enable position independent code for shared libraries
set_target_properties(EventCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Example executable
option(BUILD_EXAMPLES "Build EventCore examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests (placeholder - see README for usage examples)
option(BUILD_TESTS "Build EventCore tests" OFF)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install configuration (simplified)
install(TARGETS EventCore
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/EventCore
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp"
) 