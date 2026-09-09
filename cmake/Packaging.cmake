install(TARGETS bongo_cat
  RUNTIME DESTINATION . COMPONENT Runtime
  BUNDLE DESTINATION . COMPONENT Runtime)
if(WIN32)
  install(FILES LICENSE DESTINATION . COMPONENT Runtime)
  install(FILES resources/assets/models/LICENSE DESTINATION assets/models COMPONENT Runtime)
endif()
set(BONGO_CAT_PACKAGE_PRODUCT "BongoCat")
if(NOT BONGO_CAT_CUBISM_ENABLED)
  set(BONGO_CAT_PACKAGE_PRODUCT "BongoCat-Diagnostic")
  install(FILES cmake/DiagnosticBuildNotice.txt DESTINATION .
    COMPONENT Runtime)
endif()
if(UNIX AND NOT APPLE)
  install(DIRECTORY resources/assets DESTINATION . COMPONENT Runtime
    PATTERN "LICENSE" EXCLUDE)
endif()

include(cmake/PackagingPlatform.cmake)

set(BONGO_CAT_PACKAGE_NAME
  "${BONGO_CAT_PACKAGE_PRODUCT}-${PROJECT_VERSION}-${BONGO_CAT_PACKAGE_PLATFORM}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/bongocat-package-name.txt"
  CONTENT "${BONGO_CAT_PACKAGE_NAME}\n")

set(CPACK_PACKAGE_NAME "${BONGO_CAT_PACKAGE_PRODUCT}")
set(CPACK_PACKAGE_VENDOR "vladelaina")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${BONGO_CAT_PACKAGE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALL_CMAKE_PROJECTS
  "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")

if(WIN32)
  set(BONGO_CAT_PORTABLE_EXECUTABLE
    "${CMAKE_BINARY_DIR}/dist/${BONGO_CAT_PACKAGE_NAME}-portable.exe")
  add_custom_command(OUTPUT "${BONGO_CAT_PORTABLE_EXECUTABLE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:bongo_cat>"
      "${BONGO_CAT_PORTABLE_EXECUTABLE}"
    DEPENDS bongo_cat
    COMMENT "Building the BongoCat Windows portable executable"
    VERBATIM)
  add_custom_target(package-portable
    DEPENDS "${BONGO_CAT_PORTABLE_EXECUTABLE}")

  # CPack's stock NSIS template requests elevation and switches administrator
  # accounts to the machine-wide shell context. Generate a small, versioned
  # override from the CMake-provided template so this installer is always
  # current-user-only, including when the logged-in user belongs to
  # Administrators.
  function(bongo_cat_replace_nsis_function input_variable function_name
      replacement output_variable)
    set(source "${${input_variable}}")
    string(FIND "${source}" "Function ${function_name}" start)
    if(start LESS 0)
      message(FATAL_ERROR
        "Could not customize the CPack NSIS ${function_name} function")
    endif()
    string(SUBSTRING "${source}" ${start} -1 function_tail)
    string(FIND "${function_tail}" "FunctionEnd" relative_finish)
    if(relative_finish LESS 0)
      message(FATAL_ERROR
        "Could not find the end of the CPack NSIS ${function_name} function")
    endif()
    math(EXPR finish "${start} + ${relative_finish}")
    math(EXPR suffix_start "${finish} + 11")
    string(SUBSTRING "${source}" 0 ${start} prefix)
    string(SUBSTRING "${source}" ${suffix_start} -1 suffix)
    set(${output_variable} "${prefix}${replacement}${suffix}" PARENT_SCOPE)
  endfunction()

  set(BONGO_CAT_CPACK_TEMPLATE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/cpack-templates")
  file(MAKE_DIRECTORY "${BONGO_CAT_CPACK_TEMPLATE_DIR}")
  file(READ "${CMAKE_ROOT}/Modules/Internal/CPack/NSIS.template.in"
    BONGO_CAT_NSIS_TEMPLATE)
  string(REPLACE "\r\n" "\n" BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[RequestExecutionLevel admin]=]
    [=[RequestExecutionLevel user]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  !include "MUI.nsh"]=]
    [=[  !include "MUI.nsh"
  !include "nsDialogs.nsh"
  !include "WordFunc.nsh"
  !include "Win\RestartManager.nsh"]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  # The only choice before installation is the destination directory.
  # Shortcuts are created automatically using the fixed BongoCat folder.
  string(REPLACE [=[  !insertmacro MUI_PAGE_WELCOME]=] ""
    BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  Page custom InstallOptionsPage]=] ""
    BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE
    [=[  !insertmacro MUI_PAGE_STARTMENU Application $STARTMENU_FOLDER]=]
    [=[  !define MUI_PAGE_CUSTOMFUNCTION_PRE bongo_cat_startmenu_page_pre
  !insertmacro MUI_PAGE_STARTMENU Application $STARTMENU_FOLDER]=]
    BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE
    [=[  !insertmacro MUI_INSTALLOPTIONS_READ $INSTALL_DESKTOP "NSIS.InstallOptions.ini" "Field 5" "State"]=]
    [=[  StrCpy $INSTALL_DESKTOP "1"]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  # Silent uninstalls are used during upgrades, so user data defaults to kept.
  string(REPLACE [=[  !insertmacro MUI_UNPAGE_CONFIRM]=]
    [=[  UninstPage custom un.bongo_cat_uninstall_options_page \
    un.bongo_cat_uninstall_options_leave]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  Var IS_DEFAULT_INSTALLDIR]=]
    [=[  Var IS_DEFAULT_INSTALLDIR
  Var BONGO_CAT_UPGRADE_DIR
  Var BONGO_CAT_UPDATE_SHUTDOWN
  Var BONGO_CAT_KEEP_USER_DATA
  Var BONGO_CAT_KEEP_USER_DATA_CHECKBOX]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[  !insertmacro MUI_PAGE_DIRECTORY]=]
    [=[  !define MUI_PAGE_CUSTOMFUNCTION_PRE bongo_cat_directory_page_pre
  !insertmacro MUI_PAGE_DIRECTORY]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[Function InstallOptionsPage]=]
    [=[Function bongo_cat_directory_page_pre
  StrCmp "$BONGO_CAT_UPGRADE_DIR" "" bongo_cat_directory_page_show
  Abort
bongo_cat_directory_page_show:
FunctionEnd

Function bongo_cat_startmenu_page_pre
  Abort
FunctionEnd

Function un.bongo_cat_uninstall_options_page
  !insertmacro MUI_HEADER_TEXT "Uninstall BongoCat" \
    "Choose whether to keep your user data"
  nsDialogs::Create 1018
  Pop $0
  StrCmp "$0" "error" 0 bongo_cat_uninstall_options_ready
  Abort
bongo_cat_uninstall_options_ready:
  ${NSD_CreateLabel} 0 0 100% 24u \
    "BongoCat will be removed from this computer."
  Pop $0
  ${NSD_CreateCheckbox} 0 36u 100% 18u \
    "Keep settings, imported models, and other user data"
  Pop $BONGO_CAT_KEEP_USER_DATA_CHECKBOX
  StrCmp "$BONGO_CAT_KEEP_USER_DATA" "1" 0 \
    bongo_cat_uninstall_options_show
  ${NSD_Check} $BONGO_CAT_KEEP_USER_DATA_CHECKBOX
bongo_cat_uninstall_options_show:
  nsDialogs::Show
FunctionEnd

Function un.bongo_cat_uninstall_options_leave
  ${NSD_GetState} $BONGO_CAT_KEEP_USER_DATA_CHECKBOX \
    $BONGO_CAT_KEEP_USER_DATA
FunctionEnd

Function InstallOptionsPage]=] BONGO_CAT_NSIS_TEMPLATE
    "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_CREATE_ICONS_EXTRA@]=] [=[
  SetOutPath "$SMPROGRAMS\$STARTMENU_FOLDER"
  CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\BongoCat.lnk" \
    "$INSTDIR\.\BongoCat.exe" \
    "--nearby-root=$\"$SMPROGRAMS\$STARTMENU_FOLDER$\""
  StrCmp "$INSTALL_DESKTOP" "1" bongo_cat_create_desktop_shortcut \
    bongo_cat_shortcuts_done
bongo_cat_create_desktop_shortcut:
  SetOutPath "$DESKTOP"
  CreateShortCut "$DESKTOP\BongoCat.lnk" "$INSTDIR\.\BongoCat.exe" \
    "--nearby-root=$\"$DESKTOP$\""
bongo_cat_shortcuts_done:
  SetOutPath "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_EXTRA_INSTALL_COMMANDS@]=] [=[
  WriteRegStr SHCTX \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\BongoCat" \
    "InstallLocation" "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS@]=] [=[
  StrCmp "$BONGO_CAT_KEEP_USER_DATA" "1" bongo_cat_user_data_done
  RMDir /r "$LOCALAPPDATA\BongoCat"
bongo_cat_user_data_done:
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  string(REPLACE [=[@CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS@]=] [=[
  StrCmp "$BONGO_CAT_UPGRADE_DIR" "" bongo_cat_upgrade_ready
  StrCmp "$BONGO_CAT_UPDATE_SHUTDOWN" "1" \
    bongo_cat_signal_update_shutdown bongo_cat_restart_manager_shutdown

bongo_cat_signal_update_shutdown:
  ClearErrors
  ExecWait '"$BONGO_CAT_UPGRADE_DIR\BongoCat.exe" --shutdown-for-update' $3
  IfErrors bongo_cat_restart_manager_shutdown
  StrCmp "$3" "0" bongo_cat_uninstall_old \
    bongo_cat_restart_manager_shutdown

bongo_cat_restart_manager_shutdown:
  ; Older versions lack the update shutdown command. Use Windows Restart
  ; Manager for this exact executable instead of probing tray windows and
  ; sending guessed menu commands to legacy processes. If shutdown fails,
  ; leave the installation intact and ask the user to close the application.
  !insertmacro RestartManager_ShutdownFile \
    "$BONGO_CAT_UPGRADE_DIR\BongoCat.exe" $9
  StrCmp "$9" "0" bongo_cat_uninstall_old bongo_cat_upgrade_failed

bongo_cat_uninstall_old:
  IfFileExists "$BONGO_CAT_UPGRADE_DIR\Uninstall.exe" 0 \
    bongo_cat_upgrade_ready
  ClearErrors
  ExecWait '"$BONGO_CAT_UPGRADE_DIR\Uninstall.exe" /S \
    _?=$BONGO_CAT_UPGRADE_DIR' $3
  IfErrors bongo_cat_upgrade_failed
  StrCmp "$3" "0" bongo_cat_upgrade_ready bongo_cat_upgrade_failed

bongo_cat_upgrade_failed:
  MessageBox MB_OK|MB_ICONSTOP \
    "BongoCat could not be updated. Close the running application and try again."
  Abort

bongo_cat_upgrade_ready:
  SetOutPath "$INSTDIR"
]=] BONGO_CAT_NSIS_TEMPLATE "${BONGO_CAT_NSIS_TEMPLATE}")
  include(cmake/PackagingWindowsInit.cmake)
endif()

include(CPack)

if(WIN32)
  add_custom_target(package-installer
    COMMAND "${CMAKE_CPACK_COMMAND}" -G NSIS -C "$<CONFIG>"
      --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
      -D "CPACK_PACKAGE_FILE_NAME=${BONGO_CAT_PACKAGE_NAME}-setup"
      -D "CPACK_INCLUDE_TOPLEVEL_DIRECTORY=OFF"
    DEPENDS bongo_cat
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building the BongoCat Windows installer"
    VERBATIM)
endif()
