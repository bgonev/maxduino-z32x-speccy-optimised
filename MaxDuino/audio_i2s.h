#ifndef AUDIO_I2S_H_INCLUDED
#define AUDIO_I2S_H_INCLUDED

#include "configs.h"

#if defined(USE_PCM5120_I2S) && defined(ESP32)
void audio_i2s_begin();
void audio_i2s_start();
void audio_i2s_stop();
void audio_i2s_apply_sample_rate();
bool audio_i2s_service();
#else
static inline void audio_i2s_begin() {}
static inline void audio_i2s_start() {}
static inline void audio_i2s_stop() {}
static inline bool audio_i2s_service() { return false; }
#endif

#endif // AUDIO_I2S_H_INCLUDED
