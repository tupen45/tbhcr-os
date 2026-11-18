#ifndef BILLING_H
#define BILLING_H

#include "common.h"

void billing_init(void);
void billing_draw(int wx, int wy, int ww, int wh);
int  billing_handle_mouse(int wx, int wy, int ww, int wh, int mx, int my);
int  billing_handle_key(char key);
int  billing_tick(void);

#endif
