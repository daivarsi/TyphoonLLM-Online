#pragma once
#include <stdint.h>
int ui_platform_init(void);
uint32_t *ui_pixels(void);
void ui_present(void);
int ui_ime_open(const char *initial);
int ui_ime_poll(char *out,int size);
int ui_ime_active(void);
void ui_ime_abort(void);
void ui_platform_shutdown(void);
