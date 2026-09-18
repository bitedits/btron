/*
 * B-System (BTRON 3.20) About Box Header: about.h
 * Demoscene About Window with animated Buddhist boy monk and personages.
 */

#ifndef _BTRON_ABOUT_H_
#define _BTRON_ABOUT_H_

#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

WND* open_about_window(void);
int  about_is_animating(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_ABOUT_H_ */
