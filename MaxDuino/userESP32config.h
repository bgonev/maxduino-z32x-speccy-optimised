////////////////                 CONFIG FOR GENERIC ESP32 DEVKIT + PCM5120              ////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Default wiring for ESP32_DevKit_PCM5120:
 * SD card SPI: CS=5, SCK=18, MISO=19, MOSI=23
 * OLED I2C: SDA=21, SCL=22, address=0x3C
 * PCM5120 I2S: BCLK=26, LRCLK/WS=25, DIN=27
 * 4 buttons to GND: Play/Select=32, Stop/Back=33, Up=14, Down=13
 */

#define SD_SPI_CLOCK_SPEED SD_SCK_MHZ(4)

#ifndef USE_PCM5120_I2S
#define USE_PCM5120_I2S
#endif
#define WIFI_CONFIG_PORTAL
#define WIFI_CONFIG_AP_SSID "MaxDuino"

#define FOUR_BUTTON_NAV
#define NO_MOTOR

#define ESP32_SD_CS_PIN      5
#define ESP32_SD_SCK_PIN     18
#define ESP32_SD_MISO_PIN    19
#define ESP32_SD_MOSI_PIN    23

#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22

#define PCM5120_I2S_BCLK_PIN   26
#define PCM5120_I2S_LRCLK_PIN  25
#define PCM5120_I2S_DATA_PIN   27
#define PCM5120_I2S_SAMPLE_RATE 44100
#define PCM5120_I2S_USE_APLL
#define DEFAULT_PCM5120_I2S_AMPLITUDE 20000
#define PCM5120_I2S_VOLUME_STEP 100
#define PCM5120_I2S_VOLUME_MAX 32767

#define ESP32_BTN_PLAY_PIN   32
#define ESP32_BTN_STOP_PIN   33
#define ESP32_BTN_UP_PIN     14
#define ESP32_BTN_DOWN_PIN   13

#define FTP_SERVER_ENABLED
#define FTP_SERVER_USER "maxduino"
#define FTP_SERVER_PASSWORD "maxduino"
#define FTP_CONTROL_PORT 21
#define FTP_DATA_PORT 50009

//**************************************  OPTIONAL USE TO SAVE SPACE  ***************************************************//
#define Use_MENU
#define AYPLAY
#define MenuBLK2A
#define ID11CDTspeedup
#define ZX81SPEEDUP
#define Use_c64
#define Use_MZF
#define Use_MTX
#define Use_CAQ
#define tapORIC
    #define ORICSPEEDUP
#define Use_CAS
    #define Use_DRAGON
        #define Use_Dragon_sLeader
            #define Expand_All
#define Use_UEF
    #define Use_c112
    #define Use_hqUEF
        #define Use_c104
        //#define Use_c114
        //#define Use_c116
        #define Use_c117

#define TURBOBAUD1500

#define DEFAULT_BAUDRATE 1200
#define DEFAULT_MSELECTMASK 0
#define DEFAULT_TSXzxpUEF 0
#define DEFAULT_SKIP2A 0

//#define SERIALSCREEN
//#define FREERAM
//#define LARGEBUFFER

#define OLED_ROTATE180
#define OLED_address   0x3C
#define OLED1306
      #define OLED1306_128_64
      //#define OLED1106_1_3
      //#define video64text32

// FOUR_BUTTON_NAV maps:
// Play=Play/Select, Stop=Stop/Back, Up=previous, Down=next, Root/Menu=Stop+Down.
//#define btnRoot_AS_PIVOT
  #define SHOW_DIRPOS
      //#define SHOW_STATUS_LCD
      //#define SHOW_DIRNAMES

  #define SHOW_BLOCKPOS_LCD

//#define XY
#define XY2
#define XY2force

// Leave display counters off on ESP32 audio builds to reduce I2C work during playback.
//#define SHOW_CNTR
//#define SHOW_PCT

#define CNTRBASE 100
#define MAXPAUSE_PERIOD   1000
//#define ONPAUSE_POLCHG
#define BLOCKMODE
#define BLKSJUMPwithROOT
#define BM_BLKSJUMP 20
#define BLKBIGSIZE
#define OLEDBLKMATCH

#define TIMEOUT_RESET   60
//#define BLOCK_EEPROM_PUT
//#define BLOCKID_INTO_MEM
#define BLOCKID_NOMEM_SEARCH
#define maxblock 99
//#define BLOCKID15_IN
#define BLOCKID19_IN
#define BLOCKID21_IN
#define BLOCKTAP_IN
#define OLEDPRINTBLOCK
#define OSTATUSLINE

//#define COMPRESS_REPEAT_ROW
//#define EEPROM_LOGO_COMPRESS
#define LOAD_MEM_LOGO
//#define RECORD_EEPROM_LOGO
//#define LOAD_EEPROM_LOGO

#define LOGO_128_64 Maxduino2Alf
#define LOGO_128_32 LOGOMAXDUINO2
#define LOGO_84_48 LOGOMAXDUINO2

//#define DoubleFont

#define FONT8x8 zxFont
#define FONT8x16 cartoon8x16
