# ESP32 DevKit + PCM5120 Guide

This guide covers the custom `ESP32_DevKit_PCM5120` build for a classic ESP32 DevKit board, an SD card reader, a 128x64 I2C OLED, four buttons, and a PCM5120 I2S DAC connected to a cassette-to-AUX adapter.

## Hardware Required

* ESP32 DevKit board, not ESP32-S3
* MicroSD card reader/module with 3.3V-compatible SPI signals
* 128x64 I2C OLED, usually SSD1306 at address `0x3C`
* PCM5120 I2S DAC module
* Four momentary buttons
* Cassette-to-AUX adapter
* FAT32-formatted microSD card

All modules must share a common `GND`. Do not feed 5V logic into ESP32 GPIO pins.

## Default Wiring

These pins are defined in `MaxDuino/userESP32config.h`.

### SD Card Reader

| SD module pin | ESP32 pin |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `CS` | `GPIO5` |
| `SCK` / `CLK` | `GPIO18` |
| `MISO` / `DO` | `GPIO19` |
| `MOSI` / `DI` | `GPIO23` |

Use a 3.3V SD module or a module with proper level shifting. Many cheap SD modules marked for Arduino use 5V power but still need care with signal levels.

### OLED Display

| OLED pin | ESP32 pin |
| --- | --- |
| `VCC` | `3V3` |
| `GND` | `GND` |
| `SDA` | `GPIO21` |
| `SCL` | `GPIO22` |

The default display is SSD1306 128x64 at I2C address `0x3C`. If your display is SH1106, enable `OLED1106_1_3` in `MaxDuino/userESP32config.h`.

### PCM5120 DAC

| PCM5120 pin | ESP32 pin |
| --- | --- |
| `VCC` / `VIN` | `3V3` |
| `GND` | `GND` |
| `BCK` / `BCLK` | `GPIO26` |
| `LCK` / `LRCK` / `WS` | `GPIO25` |
| `DIN` / `DATA` | `GPIO27` |

The firmware outputs standard stereo I2S at 44.1 kHz. It does not output `MCLK`/`SCK`; use a PCM5120 module that can derive its clock from BCLK/LRCLK. Most small PCM5120 breakout boards can do this.

Connect the PCM5120 analog `L` and `R` outputs to the cassette adapter's AUX plug. Start with the volume low, then increase until the target computer loads reliably.

### Buttons

Each button connects between the ESP32 pin and `GND`. The firmware enables internal pullups.

| Function | ESP32 pin |
| --- | --- |
| Play / Select | `GPIO32` |
| Stop / Back | `GPIO33` |
| Up / Previous | `GPIO14` |
| Down / Next | `GPIO13` |

## Button Controls

The original firmware expects a root/menu button in addition to basic playback and navigation controls, so this build uses one combination:

| Action | Button |
| --- | --- |
| Play selected file / pause or resume playback | `Play` |
| Stop playback / go back from a subdirectory | `Stop` |
| Move up / previous file | `Up` |
| Move down / next file | `Down` |
| Root / menu action | `Stop + Down` |

For the `Stop + Down` combination, press both buttons together and release them after the action is accepted.

## Volume

The PCM5120 output level is controlled by the `Volume` item in the MaxDuino menu.

1. Press `Stop + Down` while playback is stopped to open the menu.
2. Use `Up` or `Down` to select `Volume`.
3. Press `Play`.
4. Use `Up` to increase volume or `Down` to decrease volume.
5. Press `Stop` to leave the volume screen.

The default amplitude is `28000`. Each Up/Down press changes it by `100`. The firmware clamps the value between `0` and `32767`.

The volume value is stored in ESP32 flash memory when changed from the menu or the WiFi configuration page, so the saved value is reused after the next power on.

## Prepare The SD Card

1. Format the microSD card as FAT32.
2. Copy supported tape files to the card, either in the root or in folders.
3. Supported examples include `.tzx`, `.tap`, `.cdt`, `.cas`, `.uef`, `.mzf`, `.caq`, `.mtx`, `.ay`, `.p`, and `.o`.
4. Insert the card before powering the ESP32.

## Build The Firmware

Install PlatformIO first. In this development workspace it was installed with Python 3.12.

From the repository root:

```powershell
pio run -e ESP32_DevKit_PCM5120
```

If `pio` is not found in the current terminal, open a new PowerShell window. On this machine the direct command is:

```powershell
& "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\pio.exe" run -e ESP32_DevKit_PCM5120
```

The compiled binary is written to:

```text
.pio\build\ESP32_DevKit_PCM5120\firmware_ESP32_DevKit_PCM5120.bin
```

## Flash The ESP32

Connect the ESP32 DevKit over USB, then check the COM port in Windows Device Manager.

Upload with PlatformIO:

```powershell
pio run -e ESP32_DevKit_PCM5120 -t upload --upload-port COMx
```

Replace `COMx` with the real port, for example:

```powershell
pio run -e ESP32_DevKit_PCM5120 -t upload --upload-port COM4
```

If upload does not start, hold the ESP32 `BOOT` button when PlatformIO shows it is connecting, then release it after flashing begins. Press `EN`/`RESET` after upload if the board does not restart automatically.

## First Power-On Check

1. Power the ESP32 from USB.
2. The OLED should initialize and show MaxDuino text or logo.
3. If the display says `No SD Card`, check SD card power, `CS`, `SCK`, `MISO`, and `MOSI`.
4. Use `Up` and `Down` to move through files.
5. Select a tape file with `Play`.
6. Put the cassette adapter in the target computer's cassette deck, connect it to the PCM5120 output, and start loading on the computer.
7. Adjust volume if loading fails. Too low or too high can both fail.

## WiFi Configuration

The ESP32 build starts an open configuration access point named:

```text
MaxDuino
```

There is no WiFi password for this configuration network.

To configure the ESP32 as a WiFi client:

1. Power on the ESP32.
2. From a phone, tablet, or computer, join the `MaxDuino` WiFi network.
3. Open:

```text
http://192.168.4.1/
```

4. Enter your home/router WiFi network name and password.
5. Press `Connect and Save`.

The firmware only saves the settings after the ESP32 successfully connects. On the next power on, it will reconnect using the saved client WiFi settings.

The same page also has a `Default volume` field. Save a value there to change the PCM5120 output level used after the next power on.

The `MaxDuino` configuration AP remains available for changing or forgetting the saved WiFi settings. Network services are serviced while playback is stopped, so do configuration and file transfers before starting tape playback.

## IP Address Menu

After the ESP32 connects to your home WiFi, open the MaxDuino menu and select `IP Address`. The display shows the client IP address assigned by your home router. If the ESP32 is not connected as a WiFi client, it shows `Not connected`.

## SD Card FTP

The ESP32 build includes a plain FTP server for managing files on the SD card.

Default login:

```text
Username: maxduino
Password: maxduino
```

From the default `MaxDuino` access point, connect your FTP client to:

```text
ftp://192.168.4.1/
```

After client WiFi is configured, the WiFi page shows the ESP32 LAN IP address. You can also connect by FTP to that LAN address.

Use plain FTP, not FTPS/SFTP. Passive mode is supported, and the server also accepts active `PORT` mode. You can upload files, download files, rename files, create directories, delete files, and delete directories. Directory deletion is recursive, so deleting a folder also deletes its contents.

Stop tape playback before transferring files. If the display file list was already open while you changed the SD card over FTP, go back to root or restart the device to refresh the visible file list.

## Changing Pins

Edit `MaxDuino/userESP32config.h` if your wiring is different:

```cpp
#define ESP32_SD_CS_PIN      5
#define ESP32_SD_SCK_PIN     18
#define ESP32_SD_MISO_PIN    19
#define ESP32_SD_MOSI_PIN    23

#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22

#define PCM5120_I2S_BCLK_PIN   26
#define PCM5120_I2S_LRCLK_PIN  25
#define PCM5120_I2S_DATA_PIN   27

#define ESP32_BTN_PLAY_PIN   32
#define ESP32_BTN_STOP_PIN   33
#define ESP32_BTN_UP_PIN     14
#define ESP32_BTN_DOWN_PIN   13
```

Rebuild and reflash after changing pins.

## Troubleshooting

### OLED is blank

Check `VCC`, `GND`, `SDA`, and `SCL`. Most modules use address `0x3C`; if yours uses `0x3D`, change `OLED_address` in `MaxDuino/userESP32config.h`.

### SD card is not detected

Use FAT32, check that the module is 3.3V-compatible, and keep SPI wires short. The build uses a conservative SD SPI clock of 4 MHz.

### No audio from PCM5120

Check `BCLK`, `LRCLK`, and `DIN`. Confirm the PCM5120 module does not require an external `MCLK` signal. This firmware does not provide `MCLK`.

### Target computer does not load

Try a different volume level. Cassette inputs are sensitive to signal level. Also confirm the tape file type is supported and that the cassette adapter is fully seated in the deck.

### Buttons behave oddly

Each button should connect only to its GPIO pin and `GND`. Do not add external pull-downs; the firmware uses internal pullups.
