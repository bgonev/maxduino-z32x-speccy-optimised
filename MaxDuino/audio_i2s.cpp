#include "audio_i2s.h"

#if defined(USE_PCM5120_I2S) && defined(ESP32)

#include <Arduino.h>
#include <cstring>
#include <driver/i2s.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>

#include "buffer.h"
#include "current_settings.h"
#include "MaxDuino.h"
#include "processing_state.h"

#ifndef PCM5120_I2S_SAMPLE_RATE
#define PCM5120_I2S_SAMPLE_RATE 44100
#endif

#ifndef PCM5120_I2S_BCLK_PIN
#define PCM5120_I2S_BCLK_PIN 26
#endif

#ifndef PCM5120_I2S_LRCLK_PIN
#define PCM5120_I2S_LRCLK_PIN 25
#endif

#ifndef PCM5120_I2S_DATA_PIN
#define PCM5120_I2S_DATA_PIN 27
#endif

#define I2S_RENDER_FRAMES 64

static constexpr i2s_port_t kI2sPort = I2S_NUM_0;
static bool i2sInstalled = false;
static bool i2sRunning = false;

static bool outputHigh = false;
static bool i2sPauseBlock = false;
static bool i2sWasPauseBlock = false;
static uint32_t directSampleLengthUs = 1;
static uint32_t remainingSamples = 0;
static int16_t currentSample = 0;

static uint32_t us_to_samples(uint32_t durationUs)
{
  uint32_t samples = ((uint64_t)durationUs * PCM5120_I2S_SAMPLE_RATE + 500000ULL) / 1000000ULL;
  return samples ? samples : 1;
}

static int16_t sample_for_level(bool high)
{
  const int16_t amplitude = (int16_t)pcm5120I2sAmplitude;
  return high ? amplitude : -amplitude;
}

static int16_t sample_for_output_level()
{
#ifdef Use_c64
  if (currentID == BLOCKID::C64TAP) {
    return sample_for_level(!outputHigh);
  }
#endif
  return sample_for_level(outputHigh);
}

static bool advance_read_pos()
{
  readpos += 2;
  if(readpos < buffsize) {
    return false;
  }

  readpos = 0;
  volatile byte * tmp = readBuffer;
  readBuffer = writeBuffer;
  writeBuffer = tmp;
  morebuff = true;
  return true;
}

static bool next_audio_segment(uint32_t &durationUs, int16_t &sample)
{
  word workingPeriod = word(readBuffer[readpos], readBuffer[readpos+1]);
  bool pauseFlipBit = false;
  bool swapped = false;

  if ((workingPeriod & HOLD_SIGNAL_MASK) == HOLD_SIGNAL_FLAG) {
    durationUs = workingPeriod & HOLD_SIGNAL_MAX_US;
    if (durationUs == 0) durationUs = 1;
    sample = sample_for_output_level();
    swapped = advance_read_pos();
    return swapped;
  }

  if (bitRead(workingPeriod, 15)) {
    bitClear(workingPeriod, 15);

    if (bitRead(workingPeriod, 13)) {
      bitClear(workingPeriod, 13);
      outputHigh = !outputHigh;
      durationUs = workingPeriod ? workingPeriod : 1;
      sample = sample_for_output_level();
      swapped = advance_read_pos();
      return swapped;
    }

    i2sPauseBlock = true;
    if (!i2sWasPauseBlock) {
      pauseFlipBit = true;
    }
  }
  else if (bitRead(workingPeriod, 14)) {
    if (bitRead(workingPeriod, 13)) {
      directSampleLengthUs = workingPeriod & 0x1fff;
      if (directSampleLengthUs == 0) directSampleLengthUs = 1;
      swapped = advance_read_pos();
      workingPeriod = word(readBuffer[readpos], readBuffer[readpos+1]);
    }

    durationUs = directSampleLengthUs;
    outputHigh = bitRead(workingPeriod, 7);
    sample = sample_for_output_level();

    if (workingPeriod & 0x0700) {
      readBuffer[readpos] = (workingPeriod >> 8) - 1;
      readBuffer[readpos+1] = (workingPeriod & 0x7f) << 1;
      return swapped;
    }

    swapped = advance_read_pos() || swapped;
    return swapped;
  }
  else if (workingPeriod == 0) {
    durationUs = 1000;
    sample = 0;
    swapped = advance_read_pos();
    return swapped;
  }

  if (pauseFlipBit || !i2sPauseBlock) {
    outputHigh = !outputHigh;
  }

  if (i2sPauseBlock) {
    if (pauseFlipBit) {
      durationUs = 1500;
      outputHigh = TSXCONTROLzxpolarityUEFSWITCHPARITY;
      workingPeriod = workingPeriod - 1;
      readBuffer[readpos] = workingPeriod / 256;
      readBuffer[readpos+1] = workingPeriod % 256;
      sample = sample_for_output_level();
      return swapped;
    }

    durationUs = ((uint32_t)workingPeriod) * 1000UL;
    if (durationUs == 0) durationUs = 1;
    i2sPauseBlock = false;
    i2sWasPauseBlock = true;
  }
  else {
    i2sWasPauseBlock = false;
    durationUs = workingPeriod ? workingPeriod : 1;
  }

  sample = sample_for_output_level();
  swapped = advance_read_pos();
  return swapped;
}

void audio_i2s_begin()
{
  if (i2sInstalled) return;

  i2s_config_t config;
  std::memset(&config, 0, sizeof(config));
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = PCM5120_I2S_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if ESP_IDF_VERSION_MAJOR >= 5
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
  config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 8;
  config.dma_buf_len = I2S_RENDER_FRAMES;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  if (i2s_driver_install(kI2sPort, &config, 0, nullptr) != ESP_OK) {
    return;
  }

  i2s_pin_config_t pins;
  std::memset(&pins, 0, sizeof(pins));
#if defined(SOC_I2S_SUPPORTS_MCLK)
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
  pins.bck_io_num = PCM5120_I2S_BCLK_PIN;
  pins.ws_io_num = PCM5120_I2S_LRCLK_PIN;
  pins.data_out_num = PCM5120_I2S_DATA_PIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kI2sPort);
    return;
  }

  i2s_zero_dma_buffer(kI2sPort);
  i2sInstalled = true;
}

void audio_i2s_start()
{
  audio_i2s_begin();
  if (!i2sInstalled) return;

  readpos = 0;
  writepos = 0;
  readBuffer = wbuffer[1];
  writeBuffer = wbuffer[0];
  morebuff = false;

  outputHigh = false;
  i2sPauseBlock = false;
  i2sWasPauseBlock = false;
  directSampleLengthUs = 1;
  remainingSamples = 0;
  currentSample = 0;

  i2s_zero_dma_buffer(kI2sPort);
  i2sRunning = true;
}

void audio_i2s_stop()
{
  if (!i2sInstalled) return;

  i2sRunning = false;
  remainingSamples = 0;
  currentSample = 0;
  i2s_zero_dma_buffer(kI2sPort);
}

bool audio_i2s_service()
{
  if (!i2sInstalled || !i2sRunning) return false;

  int16_t frames[I2S_RENDER_FRAMES * 2];
  uint16_t frame = 0;
  bool swapped = false;

  while (frame < I2S_RENDER_FRAMES) {
    if (pauseOn) {
      while (frame < I2S_RENDER_FRAMES) {
        frames[(frame * 2)] = 0;
        frames[(frame * 2) + 1] = 0;
        frame++;
      }
      break;
    }

    if (remainingSamples == 0) {
      uint32_t durationUs;
      int16_t nextSample;
      swapped = next_audio_segment(durationUs, nextSample) || swapped;
      remainingSamples = us_to_samples(durationUs);
      currentSample = nextSample;
    }

    uint32_t count = remainingSamples;
    const uint16_t framesLeft = I2S_RENDER_FRAMES - frame;
    if (count > framesLeft) count = framesLeft;

    for(uint32_t i = 0; i < count; ++i) {
      frames[(frame * 2)] = currentSample;
      frames[(frame * 2) + 1] = currentSample;
      frame++;
    }

    remainingSamples -= count;
  }

  size_t bytesWritten = 0;
  i2s_write(kI2sPort, frames, sizeof(frames), &bytesWritten, portMAX_DELAY);
  return swapped;
}

#endif // defined(USE_PCM5120_I2S) && defined(ESP32)
