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
#endif
#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
const char MENU_ITEM_IP_ADDRESS[] PROGMEM = "IP Address";
#endif
#ifndef NO_MOTOR
const char MENU_ITEM_MOTOR_CTRL[] PROGMEM = "Motor Ctrl ?";
#endif
const char MENU_ITEM_TSX[] PROGMEM = "TSXCzxpUEFSW ?";
#ifdef MenuBLK2A
const char MENU_ITEM_BLK2A[] PROGMEM = "Skip BLK:2A ?";
#endif
const char* const MENU_ITEMS[] PROGMEM = {
  MENU_ITEM_VERSION,
  MENU_ITEM_BAUD_RATE,
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  MENU_ITEM_VOLUME,
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

#if defined(USE_PCM5120_I2S) && defined(ESP32)
void doVolumeSubmenu()
{
  bool updateScreen=true;
  bool changed=false;
  lastbtn=true;
  while(!button_stop() || lastbtn) {
    if(button_up() && !lastbtn) {
      if(pcm5120I2sAmplitude <= (PCM5120_I2S_VOLUME_MAX - PCM5120_I2S_VOLUME_STEP)) {
        pcm5120I2sAmplitude += PCM5120_I2S_VOLUME_STEP;
      } else {
        pcm5120I2sAmplitude = PCM5120_I2S_VOLUME_MAX;
      }
      lastbtn=true;
      updateScreen=true;
      changed=true;
    }
    if(button_down() && !lastbtn) {
      if(pcm5120I2sAmplitude >= (PCM5120_I2S_VOLUME_MIN + PCM5120_I2S_VOLUME_STEP)) {
        pcm5120I2sAmplitude -= PCM5120_I2S_VOLUME_STEP;
      } else {
        pcm5120I2sAmplitude = PCM5120_I2S_VOLUME_MIN;
      }
      lastbtn=true;
      updateScreen=true;
      changed=true;
    }

    if(updateScreen) {
      ultoa(pcm5120I2sAmplitude, (char *)input, 10);
      printtext((char *)input, M_LINE2);
      updateScreen=false;
    }

    checkLastButton();
  }

  if(changed) {
    savePersistentPcm5120I2sAmplitude(pcm5120I2sAmplitude);
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
          doOnOffSubmenu(TSXCONTROLzxpolarityUEFSWITCHPARITY);
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
