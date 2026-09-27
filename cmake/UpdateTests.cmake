# Exercise the real worker, parser and event handling without network or UI.
add_executable(bongo_cat_update_service_tests
  tests/core/test_update_service.c tests/core/test_update.c
  src/runtime/update/update_service.c)
target_include_directories(bongo_cat_update_service_tests PRIVATE
  tests/support ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
target_include_directories(bongo_cat_update_service_tests SYSTEM PRIVATE
  ${BONGO_CAT_NUKLEAR_INCLUDE_DIR})
target_link_libraries(bongo_cat_update_service_tests PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
target_compile_definitions(bongo_cat_update_service_tests PRIVATE
  SDL_TimeToDateTime=bongo_cat_test_local_date
  SDL_OpenURL=bongo_cat_test_open_url)
foreach(scenario parsing schedule results retry exclusions concurrency shutdown)
  add_test(NAME update-${scenario}
    COMMAND bongo_cat_update_service_tests ${scenario})
  set_tests_properties(update-${scenario} PROPERTIES TIMEOUT 15 LABELS update)
endforeach()
