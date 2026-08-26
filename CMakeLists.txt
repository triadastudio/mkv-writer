cmake_minimum_required(VERSION 3.20)

project(mkv-writer VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(PROJECT_IS_TOP_LEVEL)
    option(MKV_WRITER_BUILD_TESTS "Build mkv-writer tests" ON)
else()
    option(MKV_WRITER_BUILD_TESTS "Build mkv-writer tests" OFF)
endif()

add_library(mkv-writer STATIC
    src/MkvWriter.cpp
    src/MkvWriter.h
)
add_library(mkv-writer::mkv-writer ALIAS mkv-writer)

target_compile_features(mkv-writer PUBLIC cxx_std_17)
target_include_directories(mkv-writer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(mkv-writer PROPERTIES
    CXX_EXTENSIONS OFF
    DEBUG_POSTFIX d
)

if(MSVC)
    target_compile_options(mkv-writer PRIVATE /W4 /permissive-)
else()
    target_compile_options(mkv-writer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MKV_WRITER_BUILD_TESTS)
    enable_testing()
    add_executable(mkv-writer-tests tests/MkvWriterTests.cpp)
    target_link_libraries(mkv-writer-tests PRIVATE mkv-writer::mkv-writer)
    target_compile_features(mkv-writer-tests PRIVATE cxx_std_17)
    add_test(NAME mkv-writer-tests COMMAND mkv-writer-tests)
endif()

install(TARGETS mkv-writer
    EXPORT MkvWriterTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES src/MkvWriter.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/mkv-writer-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mkv-writer-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mkv-writer
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mkv-writer-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT MkvWriterTargets
    FILE mkv-writer-targets.cmake
    NAMESPACE mkv-writer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mkv-writer
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mkv-writer-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mkv-writer-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mkv-writer
)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mkv-writer)
