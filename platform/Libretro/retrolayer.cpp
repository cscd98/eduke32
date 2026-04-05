#include "build.h"
#include "baselayer.h"
#include "retrolayer.h"
#include <setjmp.h>

#include "libretro.h"

int32_t inputchecked = 0;
char quitevent=0, appactive=1, novideo=0;
static int32_t vsync_renderlayer;
int32_t maxrefreshfreq=0;
int32_t xres=-1, yres=-1, bpp=0, fullscreen=0, bytesperline;
double refreshfreq = 59.0;
intptr_t frameplace=0;
int32_t lockcount=0;
char modechange=1;
char offscreenrendering=0;
char videomodereset = 0;
int32_t nofog=0;

#ifndef EDUKE32_GLES
static uint16_t sysgamma[3][256];
#endif
#ifdef USE_OPENGL
char nogl=0;
#endif

jmp_buf retro_nextpage_jmp;
bool    retro_nextpage_jmp_set = false;

extern bool use_hw;

uint8_t *libretro_sw_frame = nullptr;
extern GLuint drawpolyVertsID;

int32_t initsystem(void)
{
    frameplace = 0;
    lockcount = 0;

    return 0;
}

void uninitsystem(void)
{
}

void mouseGrabInput(bool grab)
{
}

int32_t wm_msgbox(const char *name, const char *fmt, ...)
{
    return 0;
}

int32_t startwin_puts(const char *s) { UNREFERENCED_PARAMETER(s); return 0; }
bool startwin_isopen(void) { return false; }

int32_t handleevents(void)
{
    timerUpdateClock();
    return 0;
}

int32_t handleevents_peekkeys(void)
{
    return 0;
}

const char *joyGetName(int32_t what, int32_t num)
{
    UNREFERENCED_PARAMETER(what);
    UNREFERENCED_PARAMETER(num);
    return "";
}

void joyScanDevices()
{
}

int32_t initinput(void(*hotplugCallback)(void) /*= nullptr*/)
{
    UNREFERENCED_PARAMETER(hotplugCallback);
    return 0;
}

void mouseUninit(void)
{    
}

void wm_setapptitle(const char *name)
{
    UNREFERENCED_PARAMETER(name);
}

void system_getcvars(void)
{}

int32_t wm_ynbox(const char *name, const char *fmt, ...)
{
    UNREFERENCED_PARAMETER(name);
    UNREFERENCED_PARAMETER(fmt);
    return 0;
}

void mouseLockToWindow(char a)
{
    UNREFERENCED_PARAMETER(a);
}

void mouseInit(void)
{
}

void uninitinput(void)
{}


char const* videoGetDisplayName(int display)
{
    UNREFERENCED_PARAMETER(display);
    return "Primary display";
}

int32_t videoSetGamma(void)
{
    return 0;
}

int32_t videoUpdatePalette(int32_t start, int32_t num)
{
    UNREFERENCED_PARAMETER(start);
    UNREFERENCED_PARAMETER(num);
    return 0;
}

void videoBeginDrawing(void)
{
    lockcount++;
#ifdef USE_OPENGL
    if (glIsBuffer(drawpolyVertsID))
        glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);
#endif
}

void videoEndDrawing(void)   {
    if (lockcount > 0) lockcount--;
}


void videoSetPalette(int32_t, int32_t, int32_t) {
}

void videoShowFrame(int32_t w)
{
    UNREFERENCED_PARAMETER(w);
#ifdef USE_OPENGL
    // Rebind VBO — it may have been unbound during rendering
    extern GLuint drawpolyVertsID;
    if (glIsBuffer(drawpolyVertsID))
        glBindBuffer(GL_ARRAY_BUFFER, drawpolyVertsID);
#endif
}

int32_t videoSetVsync(int32_t newSync)
{
    UNREFERENCED_PARAMETER(newSync);
    return 0;
}

void videoGetModes(int display)
{
    UNREFERENCED_PARAMETER(display);
}

int32_t videoSetMode(int32_t x, int32_t y, int32_t c, int32_t fs)
{
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(c);
    UNREFERENCED_PARAMETER(fs);
    return 0;
}

void libretro_alloc_sw_frame(int w, int h)
{
    free(libretro_sw_frame);

    int bytes_per_pixel = (bpp > 8) ? 4 : 1;

    libretro_sw_frame = (uint8_t *)malloc((size_t)w * h * bytes_per_pixel);

    frameplace   = (intptr_t)libretro_sw_frame;
    bytesperline = w * bytes_per_pixel;
}

void videoResetMode(void) {}

#ifdef USE_OPENGL
#include "glad/glad.h"

extern retro_hw_get_proc_address_t get_proc_address_cb;

static void *libretro_get_proc_address(const char *name)
{
    return get_proc_address_cb ? (void *)get_proc_address_cb(name) : nullptr;
}

void libretro_glad_init(void)
{
    int result = gladLoadGLLoader((GLADloadproc)libretro_get_proc_address);
}
#endif
