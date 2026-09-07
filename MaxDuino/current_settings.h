#ifndef CURRENT_SETTINGS_H_INCLUDED
#define CURRENT_SETTINGS_H_INCLUDED

#include "Arduino.h" // for types
#include "configs.h"

extern word BAUDRATE;
extern bool mselectMask;
enum class CompatibilityMode : byte { OFF = 0, ON = 1, AUTO = 2 };
extern CompatibilityMode compatibilityMode;
extern bool TSXCONTROLzxpolarityUEFSWITCHPARITY;
extern bool skip2A;
bool compatibilityEnabled();

#if defined(USE_PCM5120_I2S) && defined(ESP32)
enum class PCM5120Waveform : byte { SQUARE = 0, SINE = 1, EDGE_RAMPED = 2 };
enum class PCM5120Channel : byte { LEFT = 0, RIGHT = 1 };
#ifndef DEFAULT_PCM5120_I2S_AMPLITUDE
#define DEFAULT_PCM5120_I2S_AMPLITUDE 28000
#endif
#ifndef PCM5120_I2S_VOLUME_STEP
#define PCM5120_I2S_VOLUME_STEP 100
#endif
#ifndef PCM5120_I2S_SAMPLE_RATE
#define PCM5120_I2S_SAMPLE_RATE 44100
#endif
#ifndef PCM5120_I2S_VOLUME_MIN
#define PCM5120_I2S_VOLUME_MIN 0
#endif
#ifndef PCM5120_I2S_VOLUME_MAX
#define PCM5120_I2S_VOLUME_MAX 32767
#endif

extern word pcm5120I2sAmplitude;
extern PCM5120Waveform pcm5120Waveform;
extern PCM5120Channel pcm5120Channel;
extern uint32_t pcm5120SampleRate;
word clampPcm5120I2sAmplitude(unsigned long value);
void loadPersistentPcm5120I2sSettings();
bool savePersistentPcm5120I2sAmplitude(word value);
bool savePersistentPcm5120Waveform(PCM5120Waveform value);
bool savePersistentPcm5120Channel(PCM5120Channel value);
bool savePersistentPcm5120SampleRate(uint32_t value);
#else
static inline void loadPersistentPcm5120I2sSettings() {}
#endif

#ifdef LOAD_EEPROM_SETTINGS
void updateEEPROM();
void loadEEPROM();
#endif

#endif // CURRENT_SETTINGS_H_INCLUDED
