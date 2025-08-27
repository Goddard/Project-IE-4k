# PIE4K Installation Configuration
# This file handles cross-platform installation setup

include(GNUInstallDirs)

# Compute MinGW bin directory globally for reuse
if(WIN32)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
endif()

# Define PIE4K-specific installation directories (GemRB removed)
if(WIN32)
    set(PIE4K_BINDIR "bin")
    set(PIE4K_LIBDIR "bin")
    set(PIE4K_DATADIR "share/pie4k")
else()
    set(PIE4K_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(PIE4K_LIBDIR ${CMAKE_INSTALL_LIBDIR}/pie4k)
    set(PIE4K_DATADIR ${CMAKE_INSTALL_DATADIR}/pie4k)
endif()

# Function to install PIE4K with proper cross-platform handling
function(install_pie4k_executable target)
    # Install the main executable
    install(TARGETS ${target} 
            RUNTIME DESTINATION ${PIE4K_BINDIR}
            COMPONENT Runtime)
    
    # Set up runtime library search paths
    if(UNIX AND NOT APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN/../${PIE4K_LIBDIR}")
    elseif(WIN32)
        # No launcher script required
    endif()
endfunction()

# Function to bundle MinGW runtime DLLs (Windows only)
function(bundle_mingw_runtime target)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Get the MinGW bin directory
        get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        
        # List of common MinGW runtime DLLs
        set(MINGW_DLLS
            "libgcc_s_seh-1.dll"
            "libstdc++-6.dll"
            "libwinpthread-1.dll"
            "libgomp-1.dll"     # OpenMP
            "libiconv-2.dll"    # Character encoding conversion
        )
        
        foreach(dll ${MINGW_DLLS})
            if(EXISTS "${MINGW_BIN_DIR}/${dll}")
                install(FILES "${MINGW_BIN_DIR}/${dll}"
                        DESTINATION ${PIE4K_BINDIR}
                        COMPONENT Runtime)
                message(STATUS "Will bundle MinGW runtime: ${dll}")
            endif()
        endforeach()
    endif()
endfunction()

# Function to bundle system dependencies (Windows only)
function(bundle_system_dependencies target)
    # No-op: GemRB bundling removed; rely on fixup_bundle for pie4k only
endfunction()

# Function to deploy Python for portable use (Windows only)
function(deploy_python_portable)
    if(WIN32)
        # Deploy Python standard library for portable use
        if(EXISTS "C:/msys64/mingw64/lib/python3.12")
            install(DIRECTORY "C:/msys64/mingw64/lib/python3.12/"
                    DESTINATION "${PIE4K_BINDIR}/python3.12"
                    COMPONENT Runtime
                    PATTERN "__pycache__" EXCLUDE
                    PATTERN "*.pyc" EXCLUDE
                    PATTERN "test" EXCLUDE
                    PATTERN "tests" EXCLUDE)
        endif()
        
        # Copy Python zip file if it exists
        if(EXISTS "C:/msys64/mingw64/lib/python312.zip")
            install(FILES "C:/msys64/mingw64/lib/python312.zip"
                    DESTINATION ${PIE4K_BINDIR}
                    COMPONENT Runtime)
        endif()
    endif()
endfunction()

# Function to create a complete portable deployment (Windows only)
function(create_portable_deployment target)
    if(WIN32)
        deploy_python_portable()
        message(STATUS "Portable Windows deployment configured for ${target}")
        install(CODE "
            include(BundleUtilities)
            set(BU_CHMOD_BUNDLE_ITEMS ON)
            get_filename_component(MINGW_BIN_DIR \"${CMAKE_CXX_COMPILER}\" DIRECTORY)
            set(_search_dirs \"${MINGW_BIN_DIR};${NCNN_INSTALL_DIR}/bin;${NCNN_INSTALL_DIR}/lib\")
            file(GLOB _pie4k_extra_libs \"${CMAKE_INSTALL_PREFIX}/${PIE4K_BINDIR}/*.dll\")
            if(EXISTS \"${CMAKE_INSTALL_PREFIX}/${PIE4K_BINDIR}/pie4k.exe\")
                fixup_bundle(\"${CMAKE_INSTALL_PREFIX}/${PIE4K_BINDIR}/pie4k.exe\" \"${_pie4k_extra_libs}\" \"${_search_dirs}\")
            endif()
        ")
    endif()
endfunction()

# Function to create config file with proper paths
function(configure_pie4k_paths)
    # Create a config header with installation paths
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/pie4k_paths.h.in"
        "${CMAKE_BINARY_DIR}/generated/pie4k_paths.h"
        @ONLY
    )
endfunction()
