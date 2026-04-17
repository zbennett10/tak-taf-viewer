cmake_minimum_required(VERSION 3.16)
project(taf-viewer C)

set(CMAKE_C_STANDARD 11)

find_package(SDL2 REQUIRED)
find_package(Stb REQUIRED)

add_executable(taf-viewer
    src/main.c
    src/taf.c
    src/taf_write.c
    src/png_io.c
    src/editor.c
)

target_include_directories(taf-viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${Stb_INCLUDE_DIR}
)

if(TARGET SDL2::SDL2)
    target_link_libraries(taf-viewer PRIVATE SDL2::SDL2 SDL2::SDL2main)
else()
    target_include_directories(taf-viewer PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(taf-viewer PRIVATE ${SDL2_LIBRARIES})
endif()

if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET taf-viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:taf-viewer>
        COMMENT "Copying SDL2.dll"
    )
endif()

install(TARGETS taf-viewer RUNTIME DESTINATION bin)
