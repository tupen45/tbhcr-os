#ifndef WM_H
#define WM_H

#include "common.h"

#define WM_MAX_WINDOWS 6

void wm_init(void);
int  wm_open(int app_id);
void wm_draw_all(void);
void wm_draw_taskbar_tabs(int bar_y, int bar_h, int start_x);
int  wm_handle_mouse(int mx, int my, int pressed, int held, int released, int bar_h);
int  wm_handle_key(char key);
int  wm_tick(void);
int  wm_is_dragging(void);

#endif
