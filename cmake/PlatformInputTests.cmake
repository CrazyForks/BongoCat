# Input-wait lifecycle tests use private wake sources, without global input
# permissions, physical input devices, or a graphical session.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(bongo_cat_linux_input_wait_tests
    tests/platform/test_linux_input_wait.c src/platform/linux/linux_input_wait.c)
  target_include_directories(bongo_cat_linux_input_wait_tests PRIVATE
    src/platform/linux tests/support)
  target_link_libraries(bongo_cat_linux_input_wait_tests PRIVATE
    SDL3::SDL3-static bongo_cat_warnings)
  add_test(NAME linux-input-wait COMMAND bongo_cat_linux_input_wait_tests)
  set_tests_properties(linux-input-wait PROPERTIES TIMEOUT 15)
elseif(APPLE)
  add_executable(bongo_cat_macos_input_wait_tests
    tests/platform/test_macos_input_wait.c src/platform/macos/macos_input_wait.c)
  target_include_directories(bongo_cat_macos_input_wait_tests PRIVATE
    src/platform/macos tests/support)
  target_link_libraries(bongo_cat_macos_input_wait_tests PRIVATE
    SDL3::SDL3-static bongo_cat_warnings "-framework CoreFoundation")
  add_test(NAME macos-input-wait COMMAND bongo_cat_macos_input_wait_tests)
  set_tests_properties(macos-input-wait PROPERTIES TIMEOUT 15)
endif()
