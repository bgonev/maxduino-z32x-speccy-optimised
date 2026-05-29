#include "configs.h"
#include "Arduino.h"
#include "preferences.h"
#include "current_settings.h"
#include "casProcessing.h"

#if defined(USE_PCM5120_I2S) && defined(ESP32)
#include <nvs.h>
#include <nvs_flash.h>
#endif

word BAUDRATE = DEFAULT_BAUDRATE;
// TODO really the following should only be defined ifndef NO_MOTOR
// but the order of #includes is wrong and we only define NO_MOTOR later :-/
bool mselectMask = DEFAULT_MSELECTMASK;
bool TSXCONTROLzxpolarityUEFSWITCHPARITY = DEFAULT_TSXzxpUEF;
bool skip2A = DEFAULT_SKIP2A;

#if defined(USE_PCM5120_I2S) && defined(ESP32)
static const char PCM5120_I2S_VOLUME_NVS_KEY[] = "volume_v2";

word pcm5120I2sAmplitude = DEFAULT_PCM5120_I2S_AMPLITUDE;

word clampPcm5120I2sAmplitude(unsigned long value)
{
  if (value < PCM5120_I2S_VOLUME_MIN) return PCM5120_I2S_VOLUME_MIN;
  if (value > PCM5120_I2S_VOLUME_MAX) return PCM5120_I2S_VOLUME_MAX;
  return (word)value;
}

static bool nvs_ready()
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK) return false;
    err = nvs_flash_init();
  }
  return err == ESP_OK;
}

void loadPersistentPcm5120I2sSettings()
{
  pcm5120I2sAmplitude = clampPcm5120I2sAmplitude(pcm5120I2sAmplitude);
  if (!nvs_ready()) return;

  nvs_handle_t handle;
  if (nvs_open("audio_cfg", NVS_READONLY, &handle) != ESP_OK) return;

  uint16_t savedVolume = pcm5120I2sAmplitude;
  if (nvs_get_u16(handle, PCM5120_I2S_VOLUME_NVS_KEY, &savedVolume) == ESP_OK) {
    pcm5120I2sAmplitude = clampPcm5120I2sAmplitude(savedVolume);
  }

  nvs_close(handle);
}

bool savePersistentPcm5120I2sAmplitude(word value)
{
  pcm5120I2sAmplitude = clampPcm5120I2sAmplitude(value);
  if (!nvs_ready()) return false;

  nvs_handle_t handle;
  if (nvs_open("audio_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;

  esp_err_t err = nvs_set_u16(handle, PCM5120_I2S_VOLUME_NVS_KEY, pcm5120I2sAmplitude);
  if (err == ESP_OK) err = nvs_commit(handle);

  nvs_close(handle);
  return err == ESP_OK;
}
#endif

#ifdef LOAD_EEPROM_SETTINGS
#include "EEPROM_wrappers.h"
void updateEEPROM()
{
    /* Setting Byte: 
    *  bit 0: 1200
    *  bit 1: 2400
    *  bit 2: 3150
    *  bit 3: 3600
    *  bit 4: 3850
    *  bit 5: BLK_2A control
    *  bit 6: TSXCONTROLzxpolarityUEFSWITCHPARITY
    *  bit 7: Motor control
    */
    byte settings=0;

    switch(BAUDRATE) {
      case 1200:
      settings |=1;
      break;
      case 2400:
      settings |=2;
      break;
      case 3150:
      settings |=4;
      break;    
      case 3600:
      settings |=8;  
      break;      
      case 3850:
      settings |=16;
      break;     
    }

    #ifndef NO_MOTOR
      if(mselectMask) settings |=128;
    #endif

    if(TSXCONTROLzxpolarityUEFSWITCHPARITY) settings |=64;
    
    #ifdef MenuBLK2A
      if(skip2A) settings |=32;
    #endif

    EEPROM_put(EEPROM_CONFIG_BYTEPOS, settings);
    #ifdef Use_CAS
    setCASBaud();
    #endif
}

void loadEEPROM()
{
    byte settings=0;
    EEPROM_get(EEPROM_CONFIG_BYTEPOS, settings);
        
    if(!settings) return;
    
    #ifndef NO_MOTOR
      mselectMask=bitRead(settings,7);
    #endif

    TSXCONTROLzxpolarityUEFSWITCHPARITY=bitRead(settings,6);
    
    #ifdef MenuBLK2A
      skip2A=bitRead(settings,5);
    #endif
    
    if(bitRead(settings,0)) {
      BAUDRATE=1200;
    }
    if(bitRead(settings,1)) {
      BAUDRATE=2400;
    }
    if(bitRead(settings,2)) {
      BAUDRATE=3150;  
    }
    if(bitRead(settings,3)) {
      BAUDRATE=3600;  
    }
    if(bitRead(settings,4)) {
      BAUDRATE=3850;  
    }
}
#endif
