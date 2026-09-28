cmake_minimum_required(VERSION 3.20)
project(meshviewer LANGUAGES CXX)

# Standalone from the repository-root gateway. Configure this directory so
# vcpkg manifest mode picks up meshviewer/vcpkg.json (Assimp).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESHVIEWER_BUILD_LOADER_TEST "Build the Assimp mesh-loader test" ON)

set(MESHVIEWER_ASSIMP_TARGET "")
if(WIN32)
  find_package(assimp CONFIG REQUIRED)
  set(MESHVIEWER_ASSIMP_TARGET assimp::assimp)
elseif(MESHVIEWER_BUILD_LOADER_TEST)
  find_package(assimp CONFIG QUIET)
  if(TARGET assimp::assimp)
    set(MESHVIEWER_ASSIMP_TARGET assimp::assimp)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(ASSIMP IMPORTED_TARGET assimp)
      if(TARGET PkgConfig::ASSIMP)
        set(MESHVIEWER_ASSIMP_TARGET PkgConfig::ASSIMP)
      endif()
    endif()
  endif()
endif()

if(WIN32)
  if(NOT MSVC)
    message(FATAL_ERROR "meshviewer targets MSVC and the Windows SDK.")
  endif()

  add_executable(meshviewer WIN32
    src/main.cpp
    src/camera.cpp
    src/mesh_loader.cpp
    src/renderer.cpp
  )
  target_compile_features(meshviewer PRIVATE cxx_std_20)
  target_include_directories(meshviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_include_directories(meshviewer SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
  target_link_libraries(meshviewer PRIVATE
    ${MESHVIEWER_ASSIMP_TARGET}
    d3d12
    dxgi
    d3dcompiler
  )
  target_compile_definitions(meshviewer PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A000006
  )
  target_compile_options(meshviewer PRIVATE /W4 /utf-8)
  add_custom_command(TARGET meshviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:meshviewer>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:meshviewer>/assets"
    VERBATIM
  )
else()
  message(STATUS "meshviewer: DirectX 12 executable is Windows-only and is not built on this host")
endif()

if(MESHVIEWER_BUILD_LOADER_TEST AND NOT MESHVIEWER_ASSIMP_TARGET STREQUAL "")
  add_executable(meshviewer_loader_test
    tests/mesh_loader_test.cpp
    src/mesh_loader.cpp
  )
  target_include_directories(meshviewer_loader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(meshviewer_loader_test PRIVATE ${MESHVIEWER_ASSIMP_TARGET})
  target_compile_features(meshviewer_loader_test PRIVATE cxx_std_20)
  target_compile_definitions(meshviewer_loader_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/sample.obj" MESHVIEWER_SAMPLE_OBJ_PATH)
  target_compile_definitions(meshviewer_loader_test PRIVATE
    "MESHVIEWER_SAMPLE_OBJ=\"${MESHVIEWER_SAMPLE_OBJ_PATH}\""
  )
  if(MSVC)
    target_compile_options(meshviewer_loader_test PRIVATE /W4 /utf-8)
  else()
    target_compile_options(meshviewer_loader_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  enable_testing()
  add_test(NAME meshviewer_loader_test COMMAND meshviewer_loader_test)
elseif(MESHVIEWER_BUILD_LOADER_TEST)
  message(STATUS "meshviewer: Assimp was not found; the loader test was not added")
endif()
