# EventCore Examples

add_executable(EventCore_Example Main.cpp)

target_link_libraries(EventCore_Example 
    PRIVATE 
        EventCore
)

# Ensure C++20 standard
target_compile_features(EventCore_Example PRIVATE cxx_std_20)

# Add example-specific compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(EventCore_Example PRIVATE -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(EventCore_Example PRIVATE 
        /W4  # High warning level
        # Suppress warnings from external dependencies
        /wd26495  # Uninitialized member variable (from moodycamel)
        /wd26819  # Unannotated fallthrough (from robin_hood)
        /wd6305   # Potential sizeof/countof mismatch (from robin_hood)
    )
endif()

# Set output directory
set_target_properties(EventCore_Example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
) 