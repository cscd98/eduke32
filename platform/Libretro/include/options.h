#pragma once

#include <libretro.h>

#define RETRO_MAX_WIDTH 1920;
#define RETRO_MAX_HEIGHT 1080;

extern int g_mouse_sensitivity;
extern bool g_mouse_invert;
extern retro_core_options_v2 options_v2;
extern void apply_core_options(void);
