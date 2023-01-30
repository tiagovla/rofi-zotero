cmake_minimum_required(VERSION 3.14)
project(rofi-zotero)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(PkgConfig)
find_package(SQLite3 REQUIRED)
pkg_search_module(CAIRO REQUIRED cairo)
pkg_search_module(GLIB2 REQUIRED glib-2.0)
pkg_get_variable(ROFI_PLUGINS_DIR rofi pluginsdir)
file(GLOB SOURCES "src/*.c")

add_library(zotero SHARED ${SOURCES})
set_target_properties(zotero PROPERTIES PREFIX "")
target_link_libraries(zotero ${GLIB2_LIBRARIES} ${CAIRO_LIBRARIES}
                      SQLite::SQLite3)
target_include_directories(zotero PRIVATE src ${GLIB2_INCLUDE_DIRS}
                                          ${CAIRO_INCLUDE_DIRS})
message(STATUS ${ROFI_PLUGINS_DIR})
install(TARGETS zotero DESTINATION ${ROFI_PLUGINS_DIR})
