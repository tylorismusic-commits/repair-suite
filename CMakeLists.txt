cmake_minimum_required(VERSION 3.16)
project(repair_suite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)

set(HEARTH_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hearth-dsp"
    CACHE PATH "Path to the hearth-dsp checkout")
if(NOT EXISTS "${HEARTH_DSP_DIR}/include/hdsp/hdsp.hpp")
  message(FATAL_ERROR "hearth-dsp not found at ${HEARTH_DSP_DIR}")
endif()
include_directories("${HEARTH_DSP_DIR}/include")

enable_testing()

set(CLAP_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/clap"
    CACHE PATH "Directory where CLAP bundles are installed")
set(LV2_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/lv2"
    CACHE PATH "Directory where the LV2 bundle is installed")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

set(REPAIR_UI_SOURCES src/ui/panel.cpp)

# ---------------------------------------------------------------------------
# CLAP
# ---------------------------------------------------------------------------
add_library(repair_suite MODULE
  src/clap/repair_suite_clap.cpp
  ${REPAIR_UI_SOURCES})
target_include_directories(repair_suite PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(repair_suite PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11
  Threads::Threads ${CMAKE_DL_LIBS})
target_compile_features(repair_suite PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(repair_suite PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()
set_target_properties(repair_suite PROPERTIES
  PREFIX ""
  OUTPUT_NAME "RepairSuite"
  SUFFIX ".clap"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap")

set(REPAIR_RESOURCE_FILES
  "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt")

add_custom_target(repair_suite_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clap/RepairSuite.resources"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${REPAIR_RESOURCE_FILES}
          "${CMAKE_BINARY_DIR}/clap/RepairSuite.resources/"
  DEPENDS repair_suite ${REPAIR_RESOURCE_FILES})

install(TARGETS repair_suite LIBRARY DESTINATION "${CLAP_DIR}")
install(FILES ${REPAIR_RESOURCE_FILES}
        DESTINATION "${CLAP_DIR}/RepairSuite.resources")

# ---------------------------------------------------------------------------
# LV2
# ---------------------------------------------------------------------------
set(LV2_BUNDLE_DIR "${CMAKE_BINARY_DIR}/lv2/repair-suite.lv2")

add_library(repair_suite_lv2 MODULE src/repair_suite_lv2.cpp)
target_include_directories(repair_suite_lv2 PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(repair_suite_lv2 PRIVATE PkgConfig::LV2)
target_compile_features(repair_suite_lv2 PRIVATE cxx_std_17)
set_target_properties(repair_suite_lv2 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "repair_suite"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

add_library(repair_suite_lv2_ui MODULE src/ui/lv2_ui.cpp ${REPAIR_UI_SOURCES})
target_include_directories(repair_suite_lv2_ui PRIVATE
  "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(repair_suite_lv2_ui PRIVATE
  PkgConfig::CAIRO PkgConfig::FREETYPE PkgConfig::X11
  PkgConfig::LV2 Threads::Threads)
target_compile_features(repair_suite_lv2_ui PRIVATE cxx_std_17)
set_target_properties(repair_suite_lv2_ui PROPERTIES
  PREFIX ""
  OUTPUT_NAME "repair_suite_ui"
  LIBRARY_OUTPUT_DIRECTORY "${LV2_BUNDLE_DIR}")

add_custom_target(repair_suite_lv2_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LV2_BUNDLE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/lv2/manifest.ttl"
          "${CMAKE_SOURCE_DIR}/lv2/repair_suite.ttl"
          "${CMAKE_SOURCE_DIR}/lv2/presets.ttl"
          "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
          "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt"
          "${LV2_BUNDLE_DIR}/"
  DEPENDS repair_suite_lv2 repair_suite_lv2_ui)

add_custom_target(dist
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
  COMMAND ${CMAKE_COMMAND} -E tar czf
          "${CMAKE_BINARY_DIR}/dist/repair-suite-${PROJECT_VERSION}-linux-x86_64.tar.gz"
          lv2/repair-suite.lv2 clap/RepairSuite.clap clap/RepairSuite.resources
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  DEPENDS repair_suite_lv2_bundle repair_suite_bundle
  COMMENT "Packing repair-suite-${PROJECT_VERSION}")

install(TARGETS repair_suite_lv2 repair_suite_lv2_ui
        LIBRARY DESTINATION "${LV2_DIR}/repair-suite.lv2")
install(FILES
  lv2/manifest.ttl
  lv2/repair_suite.ttl
  lv2/presets.ttl
  assets/fonts/Jost-ExtraBold.ttf
  assets/fonts/OFL.txt
  DESTINATION "${LV2_DIR}/repair-suite.lv2")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(repair_engine_test test/test_engine.cpp)
target_include_directories(repair_engine_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(repair_engine_test PRIVATE cxx_std_17)
add_test(NAME repair_engine COMMAND repair_engine_test)

add_executable(repair_panel_test test/test_panel.cpp ${REPAIR_UI_SOURCES})
target_include_directories(repair_panel_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(repair_panel_test PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(repair_panel_test PRIVATE cxx_std_17)
add_test(NAME repair_panel COMMAND repair_panel_test)

add_executable(repair_ui_preview test/ui_preview.cpp ${REPAIR_UI_SOURCES})
target_include_directories(repair_ui_preview PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(repair_ui_preview PRIVATE PkgConfig::CAIRO PkgConfig::FREETYPE)
target_compile_features(repair_ui_preview PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# Host load tests
# ---------------------------------------------------------------------------
pkg_check_modules(LILV QUIET IMPORTED_TARGET lilv-0)

add_executable(repair_clap_host_test test/clap_host_test.cpp)
target_include_directories(repair_clap_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(repair_clap_host_test PRIVATE ${CMAKE_DL_LIBS})
target_compile_features(repair_clap_host_test PRIVATE cxx_std_17)
add_dependencies(repair_clap_host_test repair_suite_bundle)
add_test(NAME repair_clap_host
  COMMAND repair_clap_host_test "${CMAKE_BINARY_DIR}/clap/RepairSuite.clap")

if(LILV_FOUND)
  add_executable(repair_lv2_host_test test/lv2_host_test.cpp)
  target_include_directories(repair_lv2_host_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src" ${PKG_CONFIG_LV2_INCLUDE_DIRS})
  target_link_libraries(repair_lv2_host_test PRIVATE PkgConfig::LILV)
  target_compile_features(repair_lv2_host_test PRIVATE cxx_std_17)
  add_dependencies(repair_lv2_host_test repair_suite_lv2_bundle)
  add_test(NAME repair_lv2_host
    COMMAND repair_lv2_host_test "${LV2_BUNDLE_DIR}")

endif()

find_program(XVFB_RUN xvfb-run)
add_executable(repair_clap_gui_host_test test/clap_gui_host_test.cpp)
target_include_directories(repair_clap_gui_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/clap/include")
target_link_libraries(repair_clap_gui_host_test PRIVATE
  PkgConfig::X11 ${CMAKE_DL_LIBS})
target_compile_features(repair_clap_gui_host_test PRIVATE cxx_std_17)
add_dependencies(repair_clap_gui_host_test repair_suite_bundle)
if(XVFB_RUN)
  add_test(NAME repair_clap_gui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1000x24"
      "$<TARGET_FILE:repair_clap_gui_host_test>"
      "${CMAKE_BINARY_DIR}/clap/RepairSuite.clap")
else()
  add_test(NAME repair_clap_gui_host
    COMMAND repair_clap_gui_host_test "${CMAKE_BINARY_DIR}/clap/RepairSuite.clap")
endif()

add_executable(repair_lv2_ui_host_test test/lv2_ui_host_test.cpp)
target_include_directories(repair_lv2_ui_host_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src" ${PKG_CONFIG_LV2_INCLUDE_DIRS})
target_link_libraries(repair_lv2_ui_host_test PRIVATE
  PkgConfig::X11 ${CMAKE_DL_LIBS})
target_compile_features(repair_lv2_ui_host_test PRIVATE cxx_std_17)
add_dependencies(repair_lv2_ui_host_test repair_suite_lv2_bundle)
if(XVFB_RUN)
  add_test(NAME repair_lv2_ui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1000x24"
      "$<TARGET_FILE:repair_lv2_ui_host_test>"
      "${LV2_BUNDLE_DIR}/repair_suite_ui.so")
else()
  add_test(NAME repair_lv2_ui_host
    COMMAND repair_lv2_ui_host_test "${LV2_BUNDLE_DIR}/repair_suite_ui.so")
endif()

add_executable(repair_ttl_gen tools/gen_ttl.cpp)
target_include_directories(repair_ttl_gen PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(repair_ttl_gen PRIVATE cxx_std_17)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME repair_ttl
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/check_ttl.py"
            $<TARGET_FILE:repair_ttl_gen> "${CMAKE_SOURCE_DIR}")
  add_test(NAME repair_separate_help
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/repair_separate.py"
            --help)
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import PySide6"
    RESULT_VARIABLE REPAIR_PYSIDE6_RESULT
    OUTPUT_QUIET ERROR_QUIET)
  if(REPAIR_PYSIDE6_RESULT EQUAL 0)
    add_test(NAME repair_stems_gui
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_SOURCE_DIR}/tools/repair_stems_gui.py" --selftest)
    set_tests_properties(repair_stems_gui PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()

install(PROGRAMS tools/repair_separate.py DESTINATION bin
        RENAME repair-separate)
install(PROGRAMS tools/repair_stems_gui.py DESTINATION bin
        RENAME repair-stems)
configure_file(
  "${CMAKE_SOURCE_DIR}/packaging/repair-stems.desktop.in"
  "${CMAKE_BINARY_DIR}/repair-stems.desktop"
  @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/repair-stems.desktop"
        DESTINATION share/applications)

target_compile_options(repair_engine_test PRIVATE -UNDEBUG)

target_compile_options(repair_panel_test PRIVATE -UNDEBUG)

target_compile_options(repair_clap_host_test PRIVATE -UNDEBUG)

target_compile_options(repair_lv2_host_test PRIVATE -UNDEBUG)

target_compile_options(repair_clap_gui_host_test PRIVATE -UNDEBUG)

target_compile_options(repair_lv2_ui_host_test PRIVATE -UNDEBUG)
