/*
 * B-System (BTRON 3.20) System Preferences (src/apps/preferences.c)
 * Desktop, Appearance, TIP/IME, and System Settings Manager
 */

#include <btron/wnd.h>
#include <btron/libc_shim.h>

typedef struct {
    char current_theme[32];
    int tip_engine_mode;
    int display_scaling;
} SysPreferences;
