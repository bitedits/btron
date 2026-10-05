/*
 * B-System (BTRON 3.20) Workbench Desktop Shell (src/apps/workbench.c)
 * Desktop Manager, Menu Bar, and App Dispatcher
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/libc_shim.h>

void workbench_launch_app(const char *app_name) {
    if (!app_name) return;
}
