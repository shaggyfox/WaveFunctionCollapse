cmake_minimum_required(VERSION 3.12.4)
project (WaveFunctionCollapse)

  find_package(PkgConfig)
  pkg_check_modules(SDL2 sdl2 REQUIRED)
  pkg_check_modules(SDL2_IMAGE SDL2_image REQUIRED)
  SET(ENGINE_CFLAGS ${SDL2_CFLAGS} ${SDL2_IMAGE_CFLAGS}
    -O0 -ggdb -Wall -std=c99)
  SET(ENGINE_LIBRARIES ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES} m)
  SET(ENGINE_LIB_DIRS ${SDL2_LIBRARY_DIRS} ${SDL2_IMAGE_LIBRARY_DIRS})
  SET(ENGINE_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

#target_include_directories(engine PUBLIC ${ENGINE_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
#target_compile_options( PUBLIC ${ENGINE_CFLAGS} PRIVATE -Werror)
#target_link_libraries(engine PUBLIC ${ENGINE_LIBRARIES} lua tinyscheme m)
#target_link_options(engine PUBLIC ${ENGINE_CFLAGS})
#link_options(${ENGINE_CFLAGS})

add_executable(collapse collapse.c)
target_compile_options(collapse PUBLIC ${ENGINE_CFLAGS} PRIVATE -Werror)
target_link_directories(collapse PUBLIC ${ENGINE_LIB_DIRS})
target_link_libraries(collapse ${ENGINE_LIBRARIES})
target_link_options(collapse PUBLIC ${ENGINE_CFLAGS})
target_include_directories(collapse PUBLIC ${ENGINE_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
