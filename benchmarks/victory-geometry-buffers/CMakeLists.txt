cmake_minimum_required(VERSION 3.20)
project(victory_polaris_probe LANGUAGES CXX)
if(NOT MSVC OR NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Use a Windows x64 MSVC toolchain.")
endif()
foreach(kind visible culled)
  set(dxil "${CMAKE_CURRENT_SOURCE_DIR}/${kind}.dxil")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dxil}")
  file(READ "${dxil}" bytes HEX)
  string(REGEX REPLACE "(..)" "0x\\1," ${kind}_array "${bytes}")
endforeach()
file(CONFIGURE OUTPUT generated_dxil.hpp CONTENT [=[
#pragma once
inline constexpr unsigned char visibleDxil[] = {@visible_array@};
inline constexpr unsigned char culledDxil[] = {@culled_array@};
]=] @ONLY)
file(CONFIGURE OUTPUT mesh_shader.hpp CONTENT [=[
#pragma once
inline constexpr char meshShader[] = R"hlsl(
float4 vertex(float4 input : POSITION) : SV_Position { return input; }
uint pixel(uint id : SV_PrimitiveID) : SV_Target { return id + 1; }
)hlsl";
]=] @ONLY)
add_executable(polaris-720p main.cpp probe.cpp)
target_compile_features(polaris-720p PRIVATE cxx_std_17)
target_compile_definitions(polaris-720p PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(polaris-720p PRIVATE /W4 /permissive- /Brepro /utf-8)
target_link_options(polaris-720p PRIVATE /Brepro /INCREMENTAL:NO)
target_include_directories(polaris-720p PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(polaris-720p PRIVATE d3d12 dxgi d3dcompiler user32)
