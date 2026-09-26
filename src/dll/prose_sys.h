/* Threads, locks and the sound device for the DLL: Windows (threads, winmm) and POSIX (pthreads; PulseAudio or ALSA,
 * loaded at run time so that building needs neither). Not part of the firmware. */
#ifndef PROSE_SYS_H
#define PROSE_SYS_H

/* include this first: condition variables need Windows Vista's API, clock_gettime and nanosleep POSIX 2008 */
#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#elif !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION sys_mutex;
typedef CONDITION_VARIABLE sys_cond;
typedef HANDLE sys_thread;
typedef DWORD sys_thread_id;
#else
#include <pthread.h>
typedef pthread_mutex_t sys_mutex;
typedef pthread_cond_t sys_cond;
typedef pthread_t sys_thread;
typedef pthread_t sys_thread_id;
#endif

void sys_mutex_init(sys_mutex *m);
void sys_mutex_destroy(sys_mutex *m);
void sys_lock(sys_mutex *m);
void sys_unlock(sys_mutex *m);
void sys_cond_init(sys_cond *c);
void sys_cond_destroy(sys_cond *c);
void sys_cond_wait(sys_cond *c, sys_mutex *m);
void sys_cond_timed_wait(sys_cond *c, sys_mutex *m, int ms);
void sys_cond_broadcast(sys_cond *c);
int sys_thread_start(sys_thread *t, void (*fn)(void *), void *arg);
void sys_thread_join(sys_thread t);
sys_thread_id sys_thread_self(void);
int sys_thread_equal(sys_thread_id a, sys_thread_id b);
void sys_sleep_ms(int ms);

/* A mutex made on first use (for the library's global lock, which has no init call). */
sys_mutex *sys_global_mutex(void);

/* The sound device: 16-bit mono PCM at 10 kHz. write blocks until the device has room; played is the number of
 * samples it has played since open; pause/resume act at once; drop discards what is queued. */
typedef struct sys_audio sys_audio;
sys_audio *sys_audio_open(void);
int sys_audio_write(sys_audio *a, const int16_t *pcm, size_t count); /* 0, or -1 if the device failed */
uint32_t sys_audio_played(sys_audio *a);
uint32_t sys_audio_queued(sys_audio *a); /* written but not played yet */
void sys_audio_pause(sys_audio *a, int on);
void sys_audio_drop(sys_audio *a);
void sys_audio_close(sys_audio *a); /* waits until everything written has played; NULL: nothing */

#endif
