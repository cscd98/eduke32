#include "driver_libretro.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Ring buffer: 2 seconds of stereo int16 audio
// ---------------------------------------------------------------------------
static int16_t         *ring_buf     = nullptr;
static int              ring_samples = 0;
static std::atomic<int> ring_wp { 0 }, ring_rp { 0 };
using clk                           = std::chrono::steady_clock;
static clk::time_point last_service = clk::now();
static clk::time_point pump_last    = clk::now();
static double          pump_credit  = 0.0;  // frames of mixer time owed

static int ring_avail_write()
{
    int rp = ring_rp.load(std::memory_order_acquire);
    int wp = ring_wp.load(std::memory_order_relaxed);
    return (rp - wp - 1 + ring_samples) % ring_samples;
}
static int ring_avail_read()
{
    int rp = ring_rp.load(std::memory_order_relaxed);
    int wp = ring_wp.load(std::memory_order_acquire);
    return (wp - rp + ring_samples) % ring_samples;
}

// ---------------------------------------------------------------------------
// MV state
// ---------------------------------------------------------------------------
static char *mv_buffer           = nullptr;
static int   mv_divs             = 0;
static void (*mv_callback)(void) = nullptr;
static int mv_divsize            = 0;
static int mv_slot               = 1;

// ---------------------------------------------------------------------------
// Driver API
// ---------------------------------------------------------------------------
int CustomDrv_GetError(void)
{
    return 0;
}
const char *CustomDrv_ErrorString(int)
{
    return "";
}

int CustomDrv_PCM_Init(int *mixrate, int *numchannels, void *)
{
    *mixrate     = target_sample_rate;
    *numchannels = 2;

    ring_samples = (int)(target_sample_rate * 2 * 2);
    ring_buf     = (int16_t *)malloc(ring_samples * sizeof(int16_t));

    return 0;
}

void CustomDrv_PCM_Shutdown(void)
{
    mv_buffer   = nullptr;
    mv_callback = nullptr;

    if (ring_buf)
    {
        free(ring_buf);
        ring_buf = nullptr;
    }
}
static int slot_samples()
{
    return mv_divsize / 2;  // int16 samples per slot (16-bit stereo)
}

static void produce_slot()
{
    mv_callback();
    mv_slot = (mv_slot + 1) % mv_divs;

    const int16_t *src = reinterpret_cast<const int16_t *>(mv_buffer + mv_slot * mv_divsize);
    int            wp  = ring_wp.load(std::memory_order_relaxed);
    const int      n   = slot_samples();
    for (int i = 0; i < n; i++)
        ring_buf[(wp + i) % ring_samples] = src[i];
    ring_wp.store((wp + n) % ring_samples, std::memory_order_release);
}

int CustomDrv_PCM_BeginPlayback(char *buf, int bufsize, int numdivs, void (*callback)(void))
{
    last_service = pump_last = clk::now();
    pump_credit              = 0.0;
    mv_buffer                = buf;
    mv_divs                  = numdivs;
    mv_callback              = callback;
    mv_divsize               = bufsize;
    mv_slot                  = 1;
    ring_wp.store(0, std::memory_order_relaxed);
    ring_rp.store(0, std::memory_order_relaxed);

    return 0;
}

void CustomDrv_PCM_StopPlayback(void)
{
    mv_callback = nullptr;
    mv_buffer   = nullptr;
}

void CustomDrv_PCM_Lock(void)
{
}
void CustomDrv_PCM_Unlock(void)
{
}

void CustomDrv_PCM_Service(void *stream, int len)
{
    last_service = clk::now();

    int16_t  *out  = static_cast<int16_t *>(stream);
    const int want = len / (int)sizeof(int16_t);

    if (mv_callback && mv_buffer && mv_divsize > 0)
        while (ring_avail_read() < want && ring_avail_write() >= slot_samples())
            produce_slot();

    const int got = std::min(ring_avail_read(), want);
    int       rp  = ring_rp.load(std::memory_order_relaxed);
    for (int i = 0; i < got; i++)
        out[i] = ring_buf[(rp + i) % ring_samples];
    if (got < want)
        memset(out + got, 0, (want - got) * sizeof(int16_t));
    ring_rp.store((rp + got) % ring_samples, std::memory_order_release);
}

void CustomDrv_PCM_Pump(void)
{
    if (!mv_callback || !mv_buffer || mv_divsize <= 0)
        return;

    const auto   now          = clk::now();
    const double sinceService = std::chrono::duration<double>(now - last_service).count();
    const double dt           = std::min(0.25, std::chrono::duration<double>(now - pump_last).count());
    pump_last                 = now;

    const int target = (int)(target_sample_rate * 2 * 0.1);  // 100 ms of samples

    // Normal case: retro_run is consuming audio every frame
    if (sinceService < 0.05)
    {
        pump_credit = 0.0;
        while (ring_avail_read() < target && ring_avail_write() >= slot_samples())
            produce_slot();
        return;
    }

    // Stalled: the game is blocking inside one retro_run (level load, voice waits, cutscenes).
    const int slotFrames = slot_samples() / 2;
    if (slotFrames <= 0)
        return;

    pump_credit += dt * target_sample_rate;
    while (pump_credit >= slotFrames)
    {
        pump_credit -= slotFrames;
        if (ring_avail_write() < slot_samples())
            ring_rp.store((ring_rp.load(std::memory_order_relaxed) + slot_samples()) % ring_samples, std::memory_order_release);
        produce_slot();
    }
}
