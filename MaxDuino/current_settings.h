#ifndef CURRENT_SETTINGS_H_INCLUDED
#define CURRENT_SETTINGS_H_INCLUDED

#include "Arduino.h" // for types
#include "configs.h"

extern word BAUDRATE;
extern bool mselectMask;
extern bool TSXCONTROLzxpolarityUEFSWITCHPARITY;
extern bool skip2A;

#if defined(USE_PCM5120_I2S) && defined(ESP32)
#ifndef DEFAULT_PCM5120_I2S_AMPLITUDE
#define DEFAULT_PCM5120_I2S_AMPLITUDE 28000
#endif
#ifndef PCM5120_I2S_VOLUME_STEP
#define PCM5120_I2S_VOLUME_STEP 100
#endif
#ifndef PCM5120_I2S_VOLUME_MIN
#define PCM5120_I2S_VOLUME_MIN 0
#endif
#ifndef PCM5120_I2S_VOLUME_MAX
#define PCM5120_I2S_VOLUME_MAX 32767
#endif

extern word pcm5120I2sAmplitude;
word clampPcm5120I2sAmplitude(unsigned long value);
void loadPersistentPcm5120I2sSettings();
bool savePersistentPcm5120I2sAmplitude(word value);
#else
static inline void loadPersistentPcm5120I2sSettings() {}
#endif

#ifdef LOAD_EEPROM_SETTINGS
void updateEEPROM();
void loadEEPROM();
#endif

#endif // CURRENT_SETTINGS_H_INCLUDED
