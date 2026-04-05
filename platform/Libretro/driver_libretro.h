#pragma once

#include <cstdint>

#define DEFAULT_SAMPLE_RATE 48000

extern double target_sample_rate;

#ifdef __cplusplus
extern "C" {
#endif
int         CustomDrv_GetError(void);
const char *CustomDrv_ErrorString(int);
int         CustomDrv_PCM_Init(int *, int *, void *);
void        CustomDrv_PCM_Shutdown(void);
int         CustomDrv_PCM_BeginPlayback(char *, int, int, void (*)(void));
void        CustomDrv_PCM_StopPlayback(void);
void        CustomDrv_PCM_Lock(void);
void        CustomDrv_PCM_Unlock(void);
void        CustomDrv_PCM_Service(void *stream, int len);
#ifdef __cplusplus
}
#endif
