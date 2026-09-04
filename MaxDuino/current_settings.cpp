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
static const char PCM5120_I2S_WAVEFORM_NVS_KEY[] = "waveform_v1";
static const char PCM5120_I2S_CHANNEL_NVS_KEY[] = "channel_v1";
static const char PCM5120_I2S_SAMPLE_RATE_NVS_KEY[] = "rate_v1";

word pcm5120I2sAmplitude = DEFAULT_PCM5120_I2S_AMPLITUDE;
PCM5120Waveform pcm5120Waveform = PCM5120Waveform::SQUARE;
PCM5120Channel pcm5120Channel = PCM5120Channel::LEFT;
uint32_t pcm5120SampleRate = PCM5120_I2S_SAMPLE_RATE;

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
  uint8_t savedWaveform = (uint8_t)PCM5120Waveform::SQUARE;
  if (nvs_get_u8(handle, PCM5120_I2S_WAVEFORM_NVS_KEY, &savedWaveform) == ESP_OK && savedWaveform <= (uint8_t)PCM5120Waveform::EDGE_RAMPED) {
    pcm5120Waveform = (PCM5120Waveform)savedWaveform;
  }
  uint8_t savedChannel = (uint8_t)PCM5120Channel::LEFT;
  if (nvs_get_u8(handle, PCM5120_I2S_CHANNEL_NVS_KEY, &savedChannel) == ESP_OK && savedChannel <= (uint8_t)PCM5120Channel::RIGHT) {
    pcm5120Channel = (PCM5120Channel)savedChannel;
  }
  uint32_t savedRate = pcm5120SampleRate;
  if (nvs_get_u32(handle, PCM5120_I2S_SAMPLE_RATE_NVS_KEY, &savedRate) == ESP_OK &&
      (savedRate == 22050 || savedRate == 44100 || savedRate == 48000)) {
    pcm5120SampleRate = savedRate;
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

bool savePersistentPcm5120Waveform(PCM5120Waveform value)
{
  pcm5120Waveform = value;
  if (!nvs_ready()) return false;
  nvs_handle_t handle;
  if (nvs_open("audio_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(handle, PCM5120_I2S_WAVEFORM_NVS_KEY, (uint8_t)value);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

bool savePersistentPcm5120Channel(PCM5120Channel value)
{
  pcm5120Channel = value;
  if (!nvs_ready()) return false;
  nvs_handle_t handle;
  if (nvs_open("audio_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(handle, PCM5120_I2S_CHANNEL_NVS_KEY, (uint8_t)value);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

bool savePersistentPcm5120SampleRate(uint32_t value)
{
  if (value != 22050 && value != 44100 && value != 48000) return false;
  pcm5120SampleRate = value;
  if (!nvs_ready()) return false;
  nvs_handle_t handle;
  if (nvs_open("audio_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_u32(handle, PCM5120_I2S_SAMPLE_RATE_NVS_KEY, pcm5120SampleRate);
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
