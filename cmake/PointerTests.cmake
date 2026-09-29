add_executable(bongo_cat_window_pointer_tests tests/platform/test_window_pointer.c)
target_include_directories(bongo_cat_window_pointer_tests PRIVATE
  tests/support ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
target_link_libraries(bongo_cat_window_pointer_tests PRIVATE
  bongo_cat_runtime bongo_cat_warnings)
add_test(NAME window-pointer-state COMMAND bongo_cat_window_pointer_tests)
set_tests_properties(window-pointer-state PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 30)

add_executable(bongo_cat_dial_input_tests
  tests/ui/test_dial_input.c src/ui/dial/dial_input.c)
target_include_directories(bongo_cat_dial_input_tests PRIVATE
  include "${BONGO_CAT_GENERATED_INCLUDE_DIR}" tests/support
  src/ui/dial src/ui/backend)
target_include_directories(bongo_cat_dial_input_tests SYSTEM PRIVATE
  ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
target_link_libraries(bongo_cat_dial_input_tests PRIVATE
  SDL3::SDL3-static bongo_cat_warnings)
add_test(NAME dial-input-state COMMAND bongo_cat_dial_input_tests)
if(MSVC)
  target_compile_options(bongo_cat_window_pointer_tests PRIVATE /experimental:c11atomics)
  target_compile_options(bongo_cat_dial_input_tests PRIVATE /experimental:c11atomics)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(bongo_cat_dial_input_tests PRIVATE m)
endif()
