#include "bongo_cat/platform.h"

#ifndef _WIN32
bool bongo_cat_platform_update_shutdown_argument(int argc, char **argv,
    int *exit_code) {
    (void)argc;
    (void)argv;
    (void)exit_code;
    return false;
}

bool bongo_cat_platform_single_instance_take_update_shutdown(void) {
    return false;
}
#endif
