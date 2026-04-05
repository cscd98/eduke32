#include "driver_libretro.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>

// ---------------------------------------------------------------------------
// Ring buffer: 2 seconds of stereo int16 audio
// ---------------------------------------------------------------------------
static int16_t *ring_buf = nullptr;
static int ring_samples = 0;
static std::atomic<int> ring_wp{0}, ring_rp{0};

static int ring_avail_write() {
    int rp = ring_rp.load(std::memory_order_acquire);
    int wp = ring_wp.load(std::memory_order_relaxed);
    return (rp - wp - 1 + ring_samples) % ring_samples;
}
static int ring_avail_read() {
    int rp = ring_rp.load(std::memory_order_relaxed);
    int wp = ring_wp.load(std::memory_order_acquire);
    return (wp - rp + ring_samples) % ring_samples;
}

// ---------------------------------------------------------------------------
// MV state
// ---------------------------------------------------------------------------
static char  *mv_buffer   = nullptr;
static int    mv_divs     = 0;
static void (*mv_callback)(void) = nullptr;
static int    mv_divsize  = 0;
static int    mv_slot     = 1;

// ---------------------------------------------------------------------------
// Mix thread
// ---------------------------------------------------------------------------
static std::thread       mix_thread;
static std::atomic<bool> mix_running{false};

static void mix_thread_func()
{
    while (mix_running.load(std::memory_order_acquire))
    {
        if (!mv_callback || !mv_buffer || mv_divsize <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const int slot_frames  = mv_divsize / 4;
        const int slot_samples = slot_frames * 2;

        // prevents the thread blocking inside MV's callback chain
        if (ring_avail_write() < slot_samples) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }

        mv_callback();
        mv_slot = (mv_slot + 1) % mv_divs;

        const int16_t *src = reinterpret_cast<const int16_t *>(
            mv_buffer + mv_slot * mv_divsize);
        int wp = ring_wp.load(std::memory_order_relaxed);
        for (int i = 0; i < slot_samples; i++)
            ring_buf[(wp + i) % ring_samples] = src[i];
        ring_wp.store((wp + slot_samples) % ring_samples, std::memory_order_release);

        // Sleep slightly to stay ahead of consumption
        const long long us = (long long)slot_frames * 950000LL / target_sample_rate;
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
}

// ---------------------------------------------------------------------------
// Driver API
// ---------------------------------------------------------------------------
int         CustomDrv_GetError(void)   { return 0; }
const char *CustomDrv_ErrorString(int) { return ""; }

int CustomDrv_PCM_Init(int *mixrate, int *numchannels, void *)
{
    *mixrate     = target_sample_rate;
    *numchannels = 2;

    ring_samples = (int)(target_sample_rate * 2 * 2);
    ring_buf = (int16_t*)malloc(ring_samples * sizeof(int16_t));

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

int CustomDrv_PCM_BeginPlayback(char *buf, int bufsize, int numdivs, void (*callback)(void))
{
    mv_buffer   = buf;
    mv_divs     = numdivs;
    mv_callback = callback;
    mv_divsize  = bufsize;
    mv_slot     = 1;

    ring_wp.store(0, std::memory_order_relaxed);
    ring_rp.store(0, std::memory_order_relaxed);

    mix_running.store(true, std::memory_order_release);
    mix_thread = std::thread(mix_thread_func);

    return 0;
}

void CustomDrv_PCM_StopPlayback(void)
{
    mix_running.store(false, std::memory_order_release);
    if (mix_thread.joinable())
        mix_thread.join();
    mv_callback = nullptr;
    mv_buffer   = nullptr;
}

void CustomDrv_PCM_Lock(void)   {}
void CustomDrv_PCM_Unlock(void) {}

void CustomDrv_PCM_Service(void *stream, int len)
{
    int16_t  *out  = static_cast<int16_t *>(stream);
    const int want = len / sizeof(int16_t);
    const int avail = ring_avail_read();
    const int got   = (avail < want) ? avail : want;

    int rp = ring_rp.load(std::memory_order_relaxed);
    for (int i = 0; i < got; i++)
        out[i] = ring_buf[(rp + i) % ring_samples];
    if (got < want)
        memset(out + got, 0, (want - got) * sizeof(int16_t));
    ring_rp.store((rp + got) % ring_samples, std::memory_order_release);
}
