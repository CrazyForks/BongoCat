#include "bongo_cat/platform.h"

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

bool bongo_cat_platform_open_directory(const char *path) {
    if (!path || !path[0]) return false;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, NULL, 0);
    wchar_t *directory = length > 0 ?
        malloc((size_t)length * sizeof(*directory)) : NULL;
    if (!directory || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, directory, length)) {
        free(directory);
        return false;
    }
    HINSTANCE result = ShellExecuteW(NULL, L"open", directory,
        NULL, NULL, SW_SHOWNORMAL);
    free(directory);
    return (INT_PTR)result > 32;
}
bool bongo_cat_platform_reveal_path(const char *path) {
    if (!path || !path[0]) return false;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, NULL, 0);
    wchar_t *target = length > 0 ?
        malloc((size_t)length * sizeof(*target)) : NULL;
    if (!target || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, target, length)) {
        free(target);
        return false;
    }
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        free(target);
        return false;
    }
    PIDLIST_ABSOLUTE item = NULL;
    HRESULT result = SHParseDisplayName(target, NULL, &item, 0, NULL);
    if (SUCCEEDED(result))
        result = SHOpenFolderAndSelectItems(item, 0, NULL, 0);
    CoTaskMemFree(item);
    if (SUCCEEDED(initialized)) CoUninitialize();
    free(target);
    return SUCCEEDED(result);
}
#endif
