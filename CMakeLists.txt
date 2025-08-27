cmake_minimum_required(VERSION 3.16)
project(Project-IE-4k)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

include(cmake/PIE4KInstall.cmake)

find_package(PkgConfig REQUIRED)
find_package(Python3 COMPONENTS Development REQUIRED)

# NCNN
set(NCNN_VULKAN ON CACHE BOOL "Enable ncnn Vulkan backend" FORCE)
set(NCNN_SYSTEM_GLSLANG OFF CACHE BOOL "Use bundled glslang" FORCE)
set(NCNN_INSTALL_DIR ${CMAKE_SOURCE_DIR}/install/ncnn CACHE PATH "ncnn install dir" FORCE)
set(NCNN_INCLUDE_DIR ${NCNN_INSTALL_DIR}/include)
set(NCNN_LIB_DIR ${NCNN_INSTALL_DIR}/lib)

if(EXISTS "${NCNN_INSTALL_DIR}/lib/libncnn.a" AND EXISTS "${NCNN_INSTALL_DIR}/include/ncnn/net.h")
    message(STATUS "Using prebuilt ncnn at: ${NCNN_INSTALL_DIR}")
    message(STATUS "NCNN_INCLUDE_DIR set to: ${NCNN_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "NCNN not found at expected location: ${NCNN_INSTALL_DIR}. Run compile.sh first.")
endif()

if(EXISTS "${NCNN_INSTALL_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${NCNN_INSTALL_DIR}")
    message(STATUS "Added ncnn install to CMAKE_PREFIX_PATH: ${NCNN_INSTALL_DIR}")
endif()

# Find SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

# Find zlib
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB QUIET zlib)

if(ZLIB_FOUND)
    message(STATUS "zlib found: ${ZLIB_VERSION}")
else()
    find_path(ZLIB_INCLUDE_DIR zlib.h
        PATHS /usr/include /usr/local/include
        DOC "zlib include directory"
    )
    find_library(ZLIB_LIBRARY
        NAMES z
        PATHS /usr/lib /usr/local/lib /usr/lib64
        DOC "zlib library"
    )
    if(ZLIB_INCLUDE_DIR AND ZLIB_LIBRARY)
        set(ZLIB_FOUND TRUE)
        set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
        set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
        message(STATUS "zlib found manually: ${ZLIB_LIBRARY}")
    else()
        message(FATAL_ERROR "zlib not found - required for PIE4K")
    endif()
endif()

# Find libpng
pkg_check_modules(PNG QUIET libpng)

if(PNG_FOUND)
    message(STATUS "libpng found: ${PNG_VERSION}")
else()
    find_path(PNG_INCLUDE_DIR png.h
        PATHS /usr/include /usr/local/include
        DOC "libpng include directory"
    )
    find_library(PNG_LIBRARY
        NAMES png
        PATHS /usr/lib /usr/local/lib /usr/lib64
        DOC "libpng library"
    )
    if(PNG_INCLUDE_DIR AND PNG_LIBRARY)
        set(PNG_FOUND TRUE)
        set(PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR})
        set(PNG_LIBRARIES ${PNG_LIBRARY})
        message(STATUS "libpng found manually: ${PNG_LIBRARY}")
    else()
        message(FATAL_ERROR "libpng not found - required for PIE4K")
    endif()
endif()

# Find OpenCV
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV found: ${OpenCV_VERSION}")

# Find OpenMP (required by ncnn built with OpenMP)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: linking OpenMP runtime")
else()
    message(WARNING "OpenMP not found; linking may fail if prebuilt libs require OpenMP")
endif()


file(GLOB_RECURSE CORE_SOURCES "core/*.cpp")
file(GLOB_RECURSE PLUGIN_SOURCES "plugins/*.cpp")
file(GLOB_RECURSE SERVICE_SOURCES "services/*.cpp")
set(ALL_SOURCES ${PLUGIN_SOURCES} ${SERVICE_SOURCES} ${CORE_SOURCES})
add_library(projectie4k_lib STATIC ${ALL_SOURCES})
add_executable(pie4k projectie4k.cpp)

# Ensure ncnn is built before main targets (only if we created the ExternalProject)
if(TARGET ncnn_ext)
    add_dependencies(pie4k ncnn_ext)
    add_dependencies(projectie4k_lib ncnn_ext)
endif()

# Create or import ncnn target: prefer the ncnn CMake package
if(EXISTS "${NCNN_LIB_DIR}/libncnn.a")
    list(APPEND CMAKE_PREFIX_PATH "${NCNN_INSTALL_DIR}")

    find_package(ncnn CONFIG QUIET)
    if(ncnn_FOUND)
        message(STATUS "Using ncnn CMake package from install")
    else()
        message(STATUS "ncnn CMake package not found; creating fallback IMPORTED target")
        add_library(ncnn STATIC IMPORTED GLOBAL)
        set_target_properties(ncnn PROPERTIES
            IMPORTED_LOCATION "${NCNN_LIB_DIR}/libncnn.a"
            INTERFACE_INCLUDE_DIRECTORIES "${NCNN_INCLUDE_DIR}"
        )

        if(TARGET glslang::glslang)
            set_property(TARGET ncnn PROPERTY INTERFACE_LINK_LIBRARIES "glslang::glslang;glslang::SPIRV;OpenMP::OpenMP_CXX;Threads::Threads")
        else()
            set(_glslang_libs "")
            foreach(_lib_candidate "libglslang.a" "libSPIRV.a" "libGenericCodeGen.a" "libMachineIndependent.a" "libOSDependent.a" "libglslang-default-resource-limits.a")
                if(EXISTS "${NCNN_LIB_DIR}/${_lib_candidate}")
                    list(APPEND _glslang_libs "${NCNN_LIB_DIR}/${_lib_candidate}")
                endif()
            endforeach()
            if(_glslang_libs)
                set_property(TARGET ncnn PROPERTY INTERFACE_LINK_LIBRARIES "${_glslang_libs};OpenMP::OpenMP_CXX;Threads::Threads")
            endif()
        endif()

        if(TARGET ncnn_ext)
            add_dependencies(ncnn ncnn_ext)
        endif()
    endif()
endif()

target_include_directories(projectie4k_lib PRIVATE
    .
    services
    ${SDL2_INCLUDE_DIRS}
    ${NCNN_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
)

target_include_directories(pie4k PRIVATE
    .
    services
    ${SDL2_INCLUDE_DIRS}
    ${NCNN_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
)

target_include_directories(pie4k PRIVATE ${ZLIB_INCLUDE_DIRS})
target_include_directories(projectie4k_lib PRIVATE ${ZLIB_INCLUDE_DIRS})
target_compile_definitions(pie4k PRIVATE HAVE_ZLIB=1)
target_compile_definitions(projectie4k_lib PRIVATE HAVE_ZLIB=1)

target_include_directories(pie4k PRIVATE ${PNG_INCLUDE_DIRS})
target_include_directories(projectie4k_lib PRIVATE ${PNG_INCLUDE_DIRS})
target_compile_definitions(pie4k PRIVATE HAVE_PNG=1)
target_compile_definitions(projectie4k_lib PRIVATE HAVE_PNG=1)

target_include_directories(pie4k PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(projectie4k_lib PRIVATE ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(pie4k PRIVATE HAVE_OPENCV=1)
target_compile_definitions(projectie4k_lib PRIVATE HAVE_OPENCV=1)
target_compile_definitions(pie4k PRIVATE NCNN_VULKAN=1)
target_compile_definitions(projectie4k_lib PRIVATE NCNN_VULKAN=1)

target_link_libraries(pie4k PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,projectie4k_lib>
    ncnn
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${Python3_LIBRARIES}
)

# Add dynamic loading library (for dlopen/dlclose)
if(UNIX)
    target_link_libraries(projectie4k_lib PRIVATE dl)
    target_link_libraries(pie4k PRIVATE dl)
endif()

# Link required libraries
target_link_libraries(pie4k PRIVATE ${ZLIB_LIBRARIES})
target_link_libraries(projectie4k_lib PRIVATE ${ZLIB_LIBRARIES})
target_link_libraries(pie4k PRIVATE ${PNG_LIBRARIES})
target_link_libraries(projectie4k_lib PRIVATE ${PNG_LIBRARIES})

# Link Vulkan library
find_package(Vulkan REQUIRED)
if(Vulkan_FOUND)
    target_link_libraries(pie4k PRIVATE ${Vulkan_LIBRARIES})
    target_link_libraries(projectie4k_lib PRIVATE ${Vulkan_LIBRARIES})
    message(STATUS "Vulkan found: ${Vulkan_VERSION}")
else()
    message(WARNING "Vulkan not found - GPU monitoring will be disabled")
endif()

# Link OpenMP runtime to project library if available (ncnn may require it)
if(OpenMP_CXX_FOUND)
    target_link_libraries(projectie4k_lib PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(projectie4k_lib PUBLIC HAVE_OPENMP=1)
endif()

# Link required upscaler libraries
target_link_libraries(pie4k PRIVATE ${OpenCV_LIBS})
target_link_libraries(projectie4k_lib PRIVATE ${OpenCV_LIBS})

# Configure paths and install PIE4K
configure_pie4k_paths()
install_pie4k_executable(pie4k)

# Create Windows portable deployment
if(WIN32)
    create_portable_deployment(pie4k)
endif()

# Add tests subdirectory
# add_subdirectory(tests)