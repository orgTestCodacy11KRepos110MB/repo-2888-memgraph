set(memory_src_files
    new_delete.cpp
    memory_control.cpp)

find_package(Jemalloc REQUIRED)

add_library(mg-memory STATIC ${memory_src_files})
target_link_libraries(mg-memory mg-utils fmt)

if (ENABLE_JEMALLOC)
  target_link_libraries(mg-memory Jemalloc::Jemalloc)
  target_compile_definitions(mg-memory PRIVATE USE_JEMALLOC=1)
endif()
