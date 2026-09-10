/*
 * B-System (BTRON 3.20) Settings Architecture Master Header: settings.h
 * Settings Cabinet & Control Panel Applets conforming to ./b-system/settings/
 */

#ifndef _BTRON_SETTINGS_H_
#define _BTRON_SETTINGS_H_

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/language_settings.h>
#include <btron/terminal_settings.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_APP_CONTROL_PANEL = 0,
    SETTINGS_APP_LANGUAGE,
    SETTINGS_APP_APPEARANCE,
    SETTINGS_APP_DESKTOP,
    SETTINGS_APP_DISPLAY,
    SETTINGS_APP_INPUT,
    SETTINGS_APP_SOUND,
    SETTINGS_APP_NETWORK,
    SETTINGS_APP_MEDIA,
    SETTINGS_APP_SECURITY,
    SETTINGS_APP_SYSTEM,
    SETTINGS_APP_TERMINAL,
    SETTINGS_APP_COUNT
} SETTINGS_APP_ID;

typedef struct {
    const char *id_str;
    const char *title;
    const char *title_ja;
    const char *icon_symbol;
    const char *desc;
    WND* (*open_func)(void);
} SETTINGS_APP_INFO;

/* Master Control Panel & Settings Cabinet */
WND* open_control_panel_window(void);
const SETTINGS_APP_INFO* settings_get_app_info(SETTINGS_APP_ID app_id);
int settings_get_app_count(void);

/* Dedicated Settings Applets */
WND* open_language_settings_window(void);
WND* open_appearance_settings_window(void);
WND* open_desktop_settings_window(void);
WND* open_display_settings_window(void);
WND* open_input_settings_window(void);
WND* open_sound_settings_window(void);
WND* open_network_settings_window(void);
WND* open_media_settings_window(void);
WND* open_security_settings_window(void);
WND* open_system_settings_window(void);
WND* open_terminal_settings_window(void);

/* Global Appearance Preferences: Icon Display Size */
typedef enum {
    BTRON_ICON_SIZE_32 = 32,
    BTRON_ICON_SIZE_64 = 64
} BTRON_ICON_SIZE;

BTRON_ICON_SIZE appearance_get_icon_size(void);
void appearance_set_icon_size(BTRON_ICON_SIZE size);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SETTINGS_H_ */
