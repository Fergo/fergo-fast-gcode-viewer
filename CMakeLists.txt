cmake_minimum_required(VERSION 3.20)
project(FergoFastGCodeViewer VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
# GLSL stays editable as standalone files, but is embedded in the final binary.
add_executable(embed-shaders tools/embed_shaders.cpp)
set(SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/line_shaders.generated.h")
add_custom_command(OUTPUT "${SHADER_HEADER}"
  COMMAND $<TARGET_FILE:embed-shaders> "${SHADER_DIRECTORY}" "${SHADER_HEADER}"
  DEPENDS embed-shaders
    "${SHADER_DIRECTORY}/line.vert"
    "${SHADER_DIRECTORY}/line.geom"
    "${SHADER_DIRECTORY}/line.frag"
  COMMENT "Embedding GLSL shaders"
  VERBATIM)
add_executable(fergo-gcode-viewer src/main.cpp src/gcode.cpp "${SHADER_HEADER}")
target_include_directories(fergo-gcode-viewer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_target_properties(fergo-gcode-viewer PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(fergo-gcode-viewer PRIVATE SDL3::SDL3 OpenGL::GL Threads::Threads)
if(WIN32)
  enable_language(RC)
  target_sources(fergo-gcode-viewer PRIVATE src/app.rc)
  target_include_directories(fergo-gcode-viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} src)
  add_custom_command(TARGET fergo-gcode-viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:fergo-gcode-viewer>)
endif()
if(MINGW)
  target_link_options(fergo-gcode-viewer PRIVATE -static-libgcc -static-libstdc++)
endif()
add_executable(parser-tests tests/parser_tests.cpp src/gcode.cpp)
target_include_directories(parser-tests PRIVATE src)
enable_testing()
add_test(NAME parser COMMAND parser-tests)
add_executable(colors-tests tests/colors_tests.cpp)
target_include_directories(colors-tests PRIVATE src)
add_test(NAME colors COMMAND colors-tests)
# Never replace user color preferences when rebuilding an existing directory.
if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/colors.txt")
  configure_file(colors.txt colors.txt COPYONLY)
endif()
