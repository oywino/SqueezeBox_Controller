#ifndef HA_REMOTE_AUDIO_FEEDBACK_H
#define HA_REMOTE_AUDIO_FEEDBACK_H

enum audio_feedback_sound {
    AUDIO_FEEDBACK_MOVE = 1,
    AUDIO_FEEDBACK_PRESS = 2
};

int  audio_feedback_start(void);
void audio_feedback_stop(void);
void audio_feedback_play(enum audio_feedback_sound sound);

#endif
