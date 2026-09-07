/**************************************************************
 *                  Casduino Menu Code:
 *  Menu Button (was motor control button) opens menu
 *  up/down move through menu, play select, stop back
 *  Menu Options:
 *  Baud:
 *    1200
 *    2400
 *    3150
 *    3600
 *    3850
 *  
 *  MotorControl:
 *    On
 *    Off
 *  
 *  Save settings to eeprom on exit. 
 */

#include "configs.h"
#include "Arduino.h"
#include "compat.h"
#include "buttons.h"
#include "Display.h"
#include "product_strings.h"
#include "current_settings.h"
#include "utils.h"
#include "audio_i2s.h"

#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
#include <WiFi.h>
#endif

#if defined(lineaxy)
#define M_LINE2 lineaxy
#else
#define M_LINE2 1
#endif

enum MenuItems{
  VERSION,
  BAUD_RATE,
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  VOLUME,
  WAVEFORM,
  CHANNEL,
  SAMPLE_RATE,
#endif
#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
  IP_ADDRESS,
#endif
#ifndef NO_MOTOR
  MOTOR_CTL,
#endif
  TSX_POL_UEFSW,
#ifdef MenuBLK2A
  BLK2A,
#endif
  _Num_Menu_Items
};

const char MENU_ITEM_VERSION[] PROGMEM = "Version...";
const char MENU_ITEM_BAUD_RATE[] PROGMEM = "Baud Rate ?";
#if defined(USE_PCM5120_I2S) && defined(ESP32)
const char MENU_ITEM_VOLUME[] PROGMEM = "Volume";
const char MENU_ITEM_WAVEFORM[] PROGMEM = "Waveform";
const char MENU_ITEM_CHANNEL[] PROGMEM = "Audio Channel";
const char MENU_ITEM_SAMPLE_RATE[] PROGMEM = "Sample Rate";
#endif
#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
const char MENU_ITEM_IP_ADDRESS[] PROGMEM = "IP Address";
#endif
#ifndef NO_MOTOR
const char MENU_ITEM_MOTOR_CTRL[] PROGMEM = "Motor Ctrl ?";
#endif
const char MENU_ITEM_TSX[] PROGMEM = "Compatibility Mode ?";
#ifdef MenuBLK2A
const char MENU_ITEM_BLK2A[] PROGMEM = "Skip BLK:2A ?";
#endif
const char* const MENU_ITEMS[] PROGMEM = {
  MENU_ITEM_VERSION,
  MENU_ITEM_BAUD_RATE,
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  MENU_ITEM_VOLUME,
  MENU_ITEM_WAVEFORM,
  MENU_ITEM_CHANNEL,
  MENU_ITEM_SAMPLE_RATE,
#endif
#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
  MENU_ITEM_IP_ADDRESS,
#endif
#ifndef NO_MOTOR
  MENU_ITEM_MOTOR_CTRL,
#endif
  MENU_ITEM_TSX,
#ifdef MenuBLK2A
  MENU_ITEM_BLK2A,
#endif
};

const word BAUDRATES[] PROGMEM = {1200, 2400, 3150, 3600, 3850};

#if defined(USE_PCM5120_I2S) && defined(ESP32)
  #ifndef PCM5120_I2S_VOLUME_STEP
    #define PCM5120_I2S_VOLUME_STEP 100
  #endif
  #ifndef PCM5120_I2S_VOLUME_MIN
    #define PCM5120_I2S_VOLUME_MIN 0
  #endif
  #ifndef PCM5120_I2S_VOLUME_MAX
    #define PCM5120_I2S_VOLUME_MAX 32767
  #endif
#endif

void doOnOffSubmenu(bool& refVar)
{
  bool updateScreen=true;
  lastbtn=true;
  while(!button_stop() || lastbtn) {
    if(updateScreen) {
      if(refVar==0) printtextF(PSTR("off *"), M_LINE2);
      else  printtextF(PSTR("ON *"), M_LINE2);
      updateScreen=false;
    }
    
    if(button_play() && !lastbtn) {
      refVar = !refVar;
      lastbtn=true;
      updateScreen=true;
      #if defined(OLED1306) && defined(OSTATUSLINE) 
        OledStatusLine();
      #endif              
    }
    checkLastButton();
  }
}

static void doCompatibilitySubmenu()
{
  byte selected = (byte)compatibilityMode;
  bool updateScreen = true;
  lastbtn = true;
  while (!button_stop() || lastbtn) {
    if (button_up() && !lastbtn) {
      selected = selected == 0 ? 2 : selected - 1;
      lastbtn = true;
      updateScreen = true;
    }
    if (button_down() && !lastbtn) {
      selected = selected == 2 ? 0 : selected + 1;
      lastbtn = true;
      updateScreen = true;
    }
    if (button_play() && !lastbtn) {
      compatibilityMode = (CompatibilityMode)selected;
      if (compatibilityMode == CompatibilityMode::ON) {
        TSXCONTROLzxpolarityUEFSWITCHPARITY = true;
      } else if (compatibilityMode == CompatibilityMode::OFF) {
        TSXCONTROLzxpolarityUEFSWITCHPARITY = false;
      }
      lastbtn = true;
      updateScreen = true;
    }
    if (updateScreen) {
      if (selected == (byte)CompatibilityMode::OFF) printtextF(PSTR("Off *"), M_LINE2);
      else if (selected == (byte)CompatibilityMode::ON) printtextF(PSTR("On *"), M_LINE2);
      else printtextF(PSTR("Auto *"), M_LINE2);
      updateScreen = false;
    }
    checkLastButton();
  }
}

#if defined(USE_PCM5120_I2S) && defined(ESP32)
void doVolumeSubmenu()
{
  bool updateScreen=true;
  bool changed=false;
  bool navigationHeld=false;
  uint32_t navigationStarted=0;
  uint32_t nextRepeat=0;
  lastbtn=true;
  while(!button_stop() || lastbtn) {
    const bool up = button_up();
    const bool down = button_down();
    const bool navigating = up || down;
    const uint32_t now = millis();

    if (navigating) {
      if (!navigationHeld) {
        navigationHeld = true;
        navigationStarted = now;
        nextRepeat = now + 1000UL;
      }
      if (now == navigationStarted || (now - navigationStarted >= 1000UL &&
          (int32_t)(now - nextRepeat) >= 0)) {
        if (up) {
          if (pcm5120I2sAmplitude <= (PCM5120_I2S_VOLUME_MAX - PCM5120_I2S_VOLUME_STEP))
            pcm5120I2sAmplitude += PCM5120_I2S_VOLUME_STEP;
          else pcm5120I2sAmplitude = PCM5120_I2S_VOLUME_MAX;
        } else {
          if (pcm5120I2sAmplitude >= (PCM5120_I2S_VOLUME_MIN + PCM5120_I2S_VOLUME_STEP))
            pcm5120I2sAmplitude -= PCM5120_I2S_VOLUME_STEP;
          else pcm5120I2sAmplitude = PCM5120_I2S_VOLUME_MIN;
        }
        changed=true;
        updateScreen=true;
        if (now - navigationStarted >= 1000UL) nextRepeat = now + 300UL;
      }
    } else {
      navigationHeld = false;
    }

    if(updateScreen) {
      ultoa(pcm5120I2sAmplitude, (char *)input, 10);
      printtext((char *)input, M_LINE2);
      updateScreen=false;
    }

    if (button_stop()) lastbtn=true;
    else if (!button_any()) lastbtn=false;
    delay(20);
  }

  if(changed) {
    savePersistentPcm5120I2sAmplitude(pcm5120I2sAmplitude);
  }
}

void doWaveformSubmenu()
{
  bool updateScreen = true;
  lastbtn = true;
  while(!button_stop() || lastbtn) {
    if ((button_up() || button_down()) && !lastbtn) {
      if (pcm5120Waveform == PCM5120Waveform::SQUARE) pcm5120Waveform = PCM5120Waveform::SINE;
      else if (pcm5120Waveform == PCM5120Waveform::SINE) pcm5120Waveform = PCM5120Waveform::EDGE_RAMPED;
      else pcm5120Waveform = PCM5120Waveform::SQUARE;
      savePersistentPcm5120Waveform(pcm5120Waveform);
      lastbtn = true;
      updateScreen = true;
    }
    if (updateScreen) {
      if (pcm5120Waveform == PCM5120Waveform::SQUARE) printtextF(PSTR("Square *"), M_LINE2);
      else if (pcm5120Waveform == PCM5120Waveform::SINE) printtextF(PSTR("Sine *"), M_LINE2);
      else printtextF(PSTR("Edge Ramp *"), M_LINE2);
      updateScreen = false;
    }
    checkLastButton();
  }
}

void doChannelSubmenu()
{
  bool updateScreen = true;
  lastbtn = true;
  while(!button_stop() || lastbtn) {
    if ((button_up() || button_down()) && !lastbtn) {
      pcm5120Channel = pcm5120Channel == PCM5120Channel::LEFT ? PCM5120Channel::RIGHT : PCM5120Channel::LEFT;
      savePersistentPcm5120Channel(pcm5120Channel);
      lastbtn = true;
      updateScreen = true;
    }
    if (updateScreen) {
      printtextF(pcm5120Channel == PCM5120Channel::LEFT ? PSTR("Left *") : PSTR("Right *"), M_LINE2);
      updateScreen = false;
    }
    checkLastButton();
  }
}

void doSampleRateSubmenu()
{
  const uint32_t rates[] = {22050, 44100, 48000};
  byte selected = pcm5120SampleRate == 22050 ? 0 : (pcm5120SampleRate == 48000 ? 2 : 1);
  bool updateScreen = true;
  lastbtn = true;
  while(!button_stop() || lastbtn) {
    if (button_up() && !lastbtn && selected > 0) { selected--; lastbtn=true; updateScreen=true; }
    if (button_down() && !lastbtn && selected < 2) { selected++; lastbtn=true; updateScreen=true; }
    if (button_play() && !lastbtn) {
      pcm5120SampleRate = rates[selected];
      savePersistentPcm5120SampleRate(pcm5120SampleRate);
      audio_i2s_apply_sample_rate();
      lastbtn = true;
      updateScreen = true;
    }
    if (updateScreen) {
      ultoa(rates[selected], (char *)input, 10);
      printtext((char *)input, M_LINE2);
      updateScreen = false;
    }
    checkLastButton();
  }
}
#endif

#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
void doIpAddressSubmenu()
{
  bool updateScreen=true;
  lastbtn=true;
  while(!button_stop() || lastbtn) {
    if(updateScreen) {
      if(WiFi.status() == WL_CONNECTED) {
        char ipText[16];
        WiFi.localIP().toString().toCharArray(ipText, sizeof(ipText));
        printtext(ipText, M_LINE2);
      } else {
        printtextF(PSTR("Not connected"), M_LINE2);
      }
      updateScreen=false;
    }

    checkLastButton();
  }
}
#endif

void menuMode()
{ 
  byte menuItem=0;
  byte subItem=0;
  bool updateScreen=true;
  
  while(!button_stop() || lastbtn)
  {
    if(updateScreen) {
      printtextF(PSTR("Menu"),0);
      printtextF((char *)(pgm_read_ptr(&(MENU_ITEMS[menuItem]))), M_LINE2);
      updateScreen=false;
    }
    if(button_down() && !lastbtn){
      if(menuItem<MenuItems::_Num_Menu_Items-1) menuItem+=1;
      lastbtn=true;
      updateScreen=true;
    }
    if(button_up() && !lastbtn) {
      if(menuItem>0) menuItem+=-1;
      lastbtn=true;
      updateScreen=true;
    }
    if(button_play() && !lastbtn) {
      printtextF((char *)(pgm_read_ptr(&(MENU_ITEMS[menuItem]))), 0);
      switch(menuItem){
        case MenuItems::VERSION:
          printtextF(P_VERSION, M_LINE2);
          lastbtn=true;
          while(!button_stop() || lastbtn) {
            checkLastButton();
          }
        break;

        case MenuItems::BAUD_RATE:
          subItem=0;
          updateScreen=true;
          lastbtn=true;
          while(!button_stop() || lastbtn) {
            if(button_down() && !lastbtn){
              if(subItem<4) subItem+=1;
              lastbtn=true;
              updateScreen=true;
            }
            if(button_up() && !lastbtn) {
              if(subItem>0) subItem+=-1;
              lastbtn=true;
              updateScreen=true;
            }

            const word baudrate = pgm_read_word(&(BAUDRATES[subItem]));

            if(button_play() && !lastbtn) {
              BAUDRATE = baudrate;
              updateScreen=true;
              #if defined(OLED1306) && defined(OSTATUSLINE) 
                OledStatusLine();
              #endif
              lastbtn=true;
            }

            if(updateScreen) {
              ultoa(baudrate, (char *)input, 10);
              if(BAUDRATE == baudrate) {
                strcat_P((char *)input, PSTR(" *"));
              }
              printtext((char *)input, M_LINE2);
              updateScreen=false;
            }
                    
            checkLastButton();
          }
        break;

        #if defined(USE_PCM5120_I2S) && defined(ESP32)
          case MenuItems::VOLUME:
            doVolumeSubmenu();
            break;
          case MenuItems::WAVEFORM:
            doWaveformSubmenu();
            break;
          case MenuItems::CHANNEL:
            doChannelSubmenu();
            break;
          case MenuItems::SAMPLE_RATE:
            doSampleRateSubmenu();
            break;
        #endif

        #if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
          case MenuItems::IP_ADDRESS:
            doIpAddressSubmenu();
            break;
        #endif

        #ifndef NO_MOTOR
          case MenuItems::MOTOR_CTL:
            doOnOffSubmenu(mselectMask);
            break;
        #endif

        case MenuItems::TSX_POL_UEFSW:
          doCompatibilitySubmenu();
          break;
          
#ifdef MenuBLK2A
          case MenuItems::BLK2A:
            doOnOffSubmenu(skip2A);
            break;
#endif
      }
      lastbtn=true;
      updateScreen=true;
    }
    checkLastButton();
  }
  #ifdef LOAD_EEPROM_SETTINGS
    updateEEPROM();
  #endif

  debounce(button_stop);
}
