
message("${CMAKE_CURRENT_LIST_FILE}")
message("${CMAKE_CURRENT_LIST_DIR}")
message("${CMAKE_CURRENT_SOURCE_DIR}")

include_directories(BEFORE ${CMAKE_CURRENT_LIST_DIR}/src)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/src/userver)
