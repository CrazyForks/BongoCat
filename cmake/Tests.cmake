if(BUILD_TESTING)
  add_executable(bongo_cat_log_policy_tests tests/core/test_log_policy.c)
  target_link_libraries(bongo_cat_log_policy_tests PRIVATE
    bongo_cat_runtime bongo_cat_warnings)
  add_test(NAME log-policy COMMAND bongo_cat_log_policy_tests)

  add_executable(bongo_cat_core_tests
    tests/core/test_main.c
    tests/core/test_config.c
    tests/core/test_config_validation.c
    tests/core/test_language.c
    tests/core/test_input.c
    tests/core/test_models.c
    tests/core/test_mver_pointer.c
    tests/core/test_shortcut.c
    tests/core/test_update.c
    src/platform/windows/windows_keys.c)
  target_link_libraries(bongo_cat_core_tests PRIVATE
    bongo_cat_core bongo_cat_warnings $<$<PLATFORM_ID:Windows>:user32>)
  target_include_directories(bongo_cat_core_tests PRIVATE
    tests/support
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows>)
  target_compile_definitions(bongo_cat_core_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME core COMMAND bongo_cat_core_tests)

  add_executable(bongo_cat_i18n_tests tests/i18n/test_i18n.c)
  target_link_libraries(bongo_cat_i18n_tests PRIVATE
    bongo_cat_core bongo_cat_warnings)
  target_compile_definitions(bongo_cat_i18n_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME i18n COMMAND bongo_cat_i18n_tests)

  add_executable(bongo_cat_ui_tests
    tests/ui/test_nuklear.c
    src/ui/backend/nuklear_impl.c
    src/ui/rendering/ui_paint_border.c)
  target_include_directories(bongo_cat_ui_tests PRIVATE
    src/ui/backend src/ui/rendering)
  target_include_directories(bongo_cat_ui_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_ui_tests PRIVATE bongo_cat_warnings)
  add_test(NAME ui COMMAND bongo_cat_ui_tests)

  add_executable(bongo_cat_app_state_tests
    tests/core/test_app_state.c src/core/app_state.c
    src/runtime/model/model_behavior_state.c)
  target_link_libraries(bongo_cat_app_state_tests PRIVATE bongo_cat_warnings)
  target_include_directories(bongo_cat_app_state_tests PRIVATE
    "${BONGO_CAT_GENERATED_INCLUDE_DIR}" include tests/support)
  add_test(NAME app-state COMMAND bongo_cat_app_state_tests)

  set(BONGO_CAT_MVER_IMPORT_TEST_SOURCES
    tests/model_import/test_mver_import.c
    tests/model_import/test_model_import_source.c
    tests/model_import/test_mver_manifest.c
    tests/model_import/test_tauri_portable.c
    tests/model_import/test_mver_container.c
    tests/model_import/test_mver_missing_motion.c
    tests/model_import/test_mver_pointer_import.c
    tests/ui/test_preferences_text.c
    tests/model_import/test_mver_nearby_identity.c
    tests/model_import/test_mver_policy.c
    tests/model_import/test_model_import_identity.c
    tests/model_import/test_slim_package.c
    tests/model_import/test_mver_support.c)
  add_executable(bongo_cat_mver_import_tests
    ${BONGO_CAT_MVER_IMPORT_TEST_SOURCES})
  target_include_directories(bongo_cat_mver_import_tests PRIVATE
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_include_directories(bongo_cat_mver_import_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_mver_import_tests PRIVATE bongo_cat_runtime)
  target_compile_definitions(bongo_cat_mver_import_tests PRIVATE
    BONGO_CAT_NATIVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    set_property(SOURCE ${BONGO_CAT_MVER_IMPORT_TEST_SOURCES}
      APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
    set_property(SOURCE tests/platform/test_windows_capture.c APPEND PROPERTY
      COMPILE_OPTIONS "/experimental:c11atomics")
  endif()
  add_test(NAME model-import-unit COMMAND bongo_cat_mver_import_tests)

  add_executable(bongo_cat_preferences_lifecycle_tests
    tests/ui/test_preferences_lifecycle.c)
  target_include_directories(bongo_cat_preferences_lifecycle_tests PRIVATE
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_include_directories(bongo_cat_preferences_lifecycle_tests SYSTEM PRIVATE
    ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
  target_link_libraries(bongo_cat_preferences_lifecycle_tests PRIVATE bongo_cat_runtime)
  if(WIN32)
    target_sources(bongo_cat_preferences_lifecycle_tests PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc")
    add_dependencies(bongo_cat_preferences_lifecycle_tests bongo_cat_asset_pack)
  else()
    add_custom_command(TARGET bongo_cat_preferences_lifecycle_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
        "$<TARGET_FILE_DIR:bongo_cat_preferences_lifecycle_tests>/assets")
  endif()
  bongo_cat_stage_cubism_assets(bongo_cat_preferences_lifecycle_tests)
  if(MSVC)
    target_compile_options(bongo_cat_preferences_lifecycle_tests PRIVATE
      /experimental:c11atomics)
  endif()
  add_test(NAME preferences-lifecycle COMMAND bongo_cat_preferences_lifecycle_tests
    --ci-smoke --ci-ignore-global-input
    "--storage-root=${CMAKE_CURRENT_BINARY_DIR}/preferences-lifecycle-data")
  set_tests_properties(preferences-lifecycle PROPERTIES
    ENVIRONMENT "BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN=1" TIMEOUT 60)
  if(APPLE)
    set_tests_properties(preferences-lifecycle PROPERTIES DISABLED TRUE)
  endif()

  if(BONGO_CAT_CUBISM_ENABLED)
    add_test(NAME model-startup-recovery COMMAND ${CMAKE_COMMAND}
      "-DEXECUTABLE=$<TARGET_FILE:bongo_cat_preferences_lifecycle_tests>"
      "-DASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/resources/assets"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/model-startup-recovery"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckModelStartupRecovery.cmake")
    set_tests_properties(model-startup-recovery PROPERTIES TIMEOUT 60)
    if(APPLE)
      set_tests_properties(model-startup-recovery PROPERTIES DISABLED TRUE)
    endif()
    add_executable(bongo_cat_motion_state_tests
      tests/live2d/test_motion_state.cpp)
    target_include_directories(bongo_cat_motion_state_tests PRIVATE
      src/live2d tests/support)
    target_link_libraries(bongo_cat_motion_state_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME live2d-motion-state COMMAND bongo_cat_motion_state_tests)
  endif()

  if(WIN32)
    add_executable(bongo_cat_windows_input_tests
      tests/platform/test_windows_input.c
      tests/platform/test_windows_relative.c
      tests/platform/test_windows_mouse_mapping.c
      tests/platform/test_windows_pointer_detection.c
      tests/platform/test_windows_raw_receiver.c)
    if(MSVC)
      # Visual Studio can evaluate target language options as C++ with Cubism.
      set_property(SOURCE
        tests/platform/test_windows_input.c
        tests/platform/test_windows_relative.c
        tests/platform/test_windows_mouse_mapping.c
        tests/platform/test_windows_pointer_detection.c
        tests/platform/test_windows_raw_receiver.c
        APPEND PROPERTY COMPILE_OPTIONS "/experimental:c11atomics")
    endif()
    target_include_directories(bongo_cat_windows_input_tests PRIVATE
      tests/support src/platform/windows src/runtime/input)
    target_link_libraries(bongo_cat_windows_input_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings user32)
    add_test(NAME windows-input COMMAND bongo_cat_windows_input_tests)
    set_tests_properties(windows-input PROPERTIES TIMEOUT 30)

    add_executable(bongo_cat_windows_capture_tests
      tests/platform/test_windows_capture.c)
    target_include_directories(bongo_cat_windows_capture_tests PRIVATE
      src/platform/windows)
    target_link_libraries(bongo_cat_windows_capture_tests PRIVATE
      bongo_cat_runtime bongo_cat_warnings)
    add_test(NAME windows-capture COMMAND bongo_cat_windows_capture_tests)
  endif()

  if(UNIX AND NOT APPLE)
    target_link_libraries(bongo_cat_ui_tests PRIVATE m)
    target_link_libraries(bongo_cat_app_state_tests PRIVATE m)
  endif()
endif()
