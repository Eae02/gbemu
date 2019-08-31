cmake_minimum_required(VERSION 3.8)
project(gbemu)

file(GLOB_RECURSE SOURCE_FILES Src/*.cpp)

set(OUT_DIR ${CMAKE_SOURCE_DIR}/Bin/${CMAKE_BUILD_TYPE})

#Adds compile options for warnings
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	add_compile_options(/wd4250 /wd4251 /wd4068 /wd4996 /wd4275 /D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(gbemu ${SOURCE_FILES})

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)

pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

target_link_libraries(gbemu PRIVATE ZLIB::ZLIB ${CMAKE_THREAD_LIBS_INIT} ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})

target_include_directories(gbemu PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})

set_target_properties(gbemu PROPERTIES
	CXX_STANDARD 17
	ARCHIVE_OUTPUT_DIRECTORY ${OUT_DIR}
	LIBRARY_OUTPUT_DIRECTORY ${OUT_DIR}
	RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR}
	INSTALL_RPATH "$ORIGIN"
	BUILD_WITH_INSTALL_RPATH TRUE
)
