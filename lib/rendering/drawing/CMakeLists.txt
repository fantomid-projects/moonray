# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(target rendering_drawing)

set(installIncludeDir ${PACKAGE_NAME}/rendering/drawing)
set(exportGroup ${PROJECT_NAME}Targets)

add_library(${target} INTERFACE)
# Moonray::rendering_drawing
add_library(${PROJECT_NAME}::${target} ALIAS ${target})

set_property(TARGET ${target}
    PROPERTY PUBLIC_HEADER
        drawing.h)

target_include_directories(${target}
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${target})

# -------------------------------------
# Install the target and the export set
# -------------------------------------
include(GNUInstallDirs)

# install the target
install(TARGETS ${target}
    COMPONENT ${target}
    EXPORT ${exportGroup}
    LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        NAMELINK_SKIP
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${installIncludeDir}
)
