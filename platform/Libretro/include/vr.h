#pragma once
#include <stdint.h>

enum class LibretroVrMode
{
    Off,
    Stereoscopic,
    Immersive
};

extern LibretroVrMode libretro_vr_mode;

void libretro_vr_frame(bool hardware, unsigned framebuffer);
bool libretro_vr_draw(int player, int smoothratio);
void libretro_vr_destroy();
void libretro_vr_flat();
void libretro_vr_reset_tracking();
void libretro_vr_camera(int32_t *x, int32_t *y, int32_t *z, int32_t *angle, int16_t *sector);
bool libretro_vr_angles(float *sine, float *cosine);
void libretro_vr_blend(unsigned source, unsigned dest);
