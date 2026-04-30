#include "audio_feedback.h"

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PCM_DEVICE "hw:0,0"
#define AUDIO_RATE 22050U
#define AUDIO_CHANNELS 2U
#define FRAME_SAMPLES AUDIO_CHANNELS
#define MOVE_MS 10U
#define PRESS_MS 16U
#define MOVE_HZ 950U
#define PRESS_HZ 1450U
#define STREAM_IDLE_MS 2000ULL
#define STREAM_CHUNK_FRAMES 128U
#define STREAM_BUFFER_TIME_US 80000U

struct beep {
    int16_t *pcm;
    snd_pcm_uframes_t frames;
};

static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_started = 0;
static int g_running = 0;
static enum audio_feedback_sound g_pending = 0;
static uint64_t g_last_request_ms = 0;
static snd_pcm_t *g_pcm = NULL;
static struct beep g_move;
static struct beep g_press;

static void alsa_quiet_error_handler(const char *file,
                                     int line,
                                     const char *function,
                                     int err,
                                     const char *fmt,
                                     ...)
{
    (void)file;
    (void)line;
    (void)function;
    (void)err;
    (void)fmt;
}

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void beep_free(struct beep *b)
{
    if (!b) return;
    free(b->pcm);
    b->pcm = NULL;
    b->frames = 0;
}

static int beep_make(struct beep *b, unsigned int freq_hz, unsigned int duration_ms)
{
    snd_pcm_uframes_t frames = (AUDIO_RATE * duration_ms) / 1000U;
    int16_t *pcm = (int16_t *)calloc((size_t)frames * FRAME_SAMPLES, sizeof(int16_t));
    if (!pcm) return -1;

    unsigned int half_period = AUDIO_RATE / (freq_hz * 2U);
    if (half_period == 0) half_period = 1;

    for (snd_pcm_uframes_t i = 0; i < frames; ++i) {
        unsigned int phase = ((unsigned int)i / half_period) & 1U;
        int32_t amp = phase ? 4200 : -4200;

        if (i < 40) amp = (amp * (int32_t)i) / 40;
        if (frames > 40 && i > frames - 40) amp = (amp * (int32_t)(frames - i)) / 40;

        pcm[(i * 2) + 0] = (int16_t)amp;
        pcm[(i * 2) + 1] = (int16_t)amp;
    }

    b->pcm = pcm;
    b->frames = frames;
    return 0;
}

static int pcm_open_configured(void)
{
    if (g_pcm) return 0;

    setenv("ALSA_CONFIG_PATH", "/usr/share/alsa/alsa.conf", 1);

    int rc = snd_pcm_open(&g_pcm, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) return rc;

    snd_pcm_hw_params_t *hw = NULL;
    snd_pcm_hw_params_alloca(&hw);

    rc = snd_pcm_hw_params_any(g_pcm, hw);
    if (rc < 0) return rc;
    rc = snd_pcm_hw_params_set_rate_resample(g_pcm, hw, 1);
    if (rc < 0) return rc;
    rc = snd_pcm_hw_params_set_access(g_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0) return rc;
    rc = snd_pcm_hw_params_set_format(g_pcm, hw, SND_PCM_FORMAT_S16_LE);
    if (rc < 0) return rc;
    rc = snd_pcm_hw_params_set_channels(g_pcm, hw, AUDIO_CHANNELS);
    if (rc < 0) return rc;

    unsigned int rate = AUDIO_RATE;
    rc = snd_pcm_hw_params_set_rate_near(g_pcm, hw, &rate, 0);
    if (rc < 0) return rc;

    unsigned int periods = 2;
    rc = snd_pcm_hw_params_set_periods_near(g_pcm, hw, &periods, 0);
    if (rc < 0) return rc;

    unsigned int buffer_time = STREAM_BUFFER_TIME_US;
    rc = snd_pcm_hw_params_set_buffer_time_near(g_pcm, hw, &buffer_time, 0);
    if (rc < 0) return rc;

    rc = snd_pcm_hw_params(g_pcm, hw);
    if (rc < 0) return rc;

    return snd_pcm_prepare(g_pcm);
}

static void pcm_close_stream(void)
{
    if (!g_pcm) return;
    snd_pcm_drop(g_pcm);
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
}

static struct beep *beep_for(enum audio_feedback_sound sound)
{
    if (sound == AUDIO_FEEDBACK_MOVE) return &g_move;
    if (sound == AUDIO_FEEDBACK_PRESS) return &g_press;
    return NULL;
}

static int pcm_write_frames(const int16_t *pcm, snd_pcm_uframes_t frames)
{
    snd_pcm_uframes_t done = 0;
    while (done < frames) {
        snd_pcm_sframes_t wrote = snd_pcm_writei(g_pcm, pcm + (done * FRAME_SAMPLES), frames - done);
        if (wrote < 0) {
            wrote = snd_pcm_recover(g_pcm, (int)wrote, 0);
            if (wrote < 0) return -1;
            continue;
        }
        if (wrote == 0) return -1;
        done += (snd_pcm_uframes_t)wrote;
    }
    return 0;
}

static void *audio_worker(void *arg)
{
    (void)arg;

    int16_t chunk[STREAM_CHUNK_FRAMES * FRAME_SAMPLES];
    struct beep *active = NULL;
    snd_pcm_uframes_t active_pos = 0;

    for (;;) {
        pthread_mutex_lock(&g_lock);
        while (g_running && g_pending == 0) {
            pthread_cond_wait(&g_cond, &g_lock);
        }

        if (!g_running) {
            pthread_mutex_unlock(&g_lock);
            break;
        }
        pthread_mutex_unlock(&g_lock);

        int rc = pcm_open_configured();
        if (rc < 0) {
            fprintf(stderr, "audio_feedback: pcm setup failed rc=%d %s\n", rc, snd_strerror(rc));
            pcm_close_stream();
            continue;
        }

        while (1) {
            pthread_mutex_lock(&g_lock);
            if (!g_running) {
                pthread_mutex_unlock(&g_lock);
                break;
            }

            if (!active && g_pending != 0) {
                active = beep_for(g_pending);
                active_pos = 0;
                g_pending = 0;
            }

            uint64_t idle_until = g_last_request_ms + STREAM_IDLE_MS;
            pthread_mutex_unlock(&g_lock);

            memset(chunk, 0, sizeof(chunk));

            if (active && active->pcm && active_pos < active->frames) {
                snd_pcm_uframes_t remaining = active->frames - active_pos;
                snd_pcm_uframes_t n = remaining < STREAM_CHUNK_FRAMES ? remaining : STREAM_CHUNK_FRAMES;
                memcpy(chunk, active->pcm + (active_pos * FRAME_SAMPLES),
                       (size_t)n * FRAME_SAMPLES * sizeof(int16_t));
                active_pos += n;
                if (active_pos >= active->frames) {
                    active = NULL;
                    active_pos = 0;
                }
            }

            if (pcm_write_frames(chunk, STREAM_CHUNK_FRAMES) != 0) {
                pcm_close_stream();
                break;
            }

            pthread_mutex_lock(&g_lock);
            int should_idle_stop = g_running && g_pending == 0 && !active && ms_now() >= idle_until;
            pthread_mutex_unlock(&g_lock);
            if (should_idle_stop) {
                pcm_close_stream();
                break;
            }
        }
    }

    pcm_close_stream();
    return NULL;
}

int audio_feedback_start(void)
{
    snd_lib_error_set_handler(alsa_quiet_error_handler);

    if (beep_make(&g_move, MOVE_HZ, MOVE_MS) != 0) return -1;
    if (beep_make(&g_press, PRESS_HZ, PRESS_MS) != 0) {
        beep_free(&g_move);
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    g_running = 1;
    g_pending = 0;
    g_last_request_ms = 0;
    pthread_mutex_unlock(&g_lock);

    if (pthread_create(&g_thread, NULL, audio_worker, NULL) != 0) {
        audio_feedback_stop();
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    g_started = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void audio_feedback_stop(void)
{
    pthread_mutex_lock(&g_lock);
    int started = g_started;
    g_running = 0;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);

    if (started) pthread_join(g_thread, NULL);

    pcm_close_stream();
    beep_free(&g_move);
    beep_free(&g_press);

    pthread_mutex_lock(&g_lock);
    g_started = 0;
    g_pending = 0;
    g_last_request_ms = 0;
    pthread_mutex_unlock(&g_lock);
}

void audio_feedback_play(enum audio_feedback_sound sound)
{
    if (!beep_for(sound)) return;

    pthread_mutex_lock(&g_lock);
    if (g_running && g_pending == 0) {
        g_pending = sound;
    }
    g_last_request_ms = ms_now();
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);
}
