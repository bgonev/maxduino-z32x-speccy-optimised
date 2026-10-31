#include "audio_i2s.h"

#if defined(USE_PCM5120_I2S) && defined(ESP32)

#include <Arduino.h>
#include <cstring>
#include <cmath>
#include <driver/i2s.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>

#include "buffer.h"
#include "current_settings.h"
#include "MaxDuino.h"
#include "MaxProcessing.h"
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
static uint32_t currentSegmentSamples = 1;
static uint32_t currentSegmentOffset = 0;
static bool currentSegmentCanShape = false;
static uint32_t sampleRemainder = 0;
static bool stopAfterDrain = false;
static uint32_t stopAfterDrainDeadline = 0;

// The producer can finish the file while a final period is still queued in
// the render path.  Keep I2S alive briefly so that period is transmitted, but
// always shut it down afterwards instead of servicing stale buffers forever.
static constexpr uint32_t I2S_EOF_DRAIN_MS = 1500;

static uint32_t audio_sample_rate()
{
  return pcm5120SampleRate;
}

static uint32_t us_to_samples(uint32_t durationUs)
{
  const uint64_t scaled = (uint64_t)durationUs * audio_sample_rate() + sampleRemainder;
  uint32_t samples = scaled / 1000000ULL;
  sampleRemainder = scaled % 1000000ULL;
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

static bool next_audio_segment(uint32_t &durationUs, int16_t &sample, bool &canShape)
{
  canShape = false;
  word workingPeriod = word(readBuffer[readpos], readBuffer[readpos+1]);
  bool pauseFlipBit = false;
  bool swapped = false;

  // TZX 0x2B level marker: update the output state without a pulse.
  if ((workingPeriod & 0xF000) == LEVEL_SIGNAL_FLAG) {
    outputHigh = workingPeriod & 1;
    swapped = advance_read_pos();
    durationUs = 1;
    sample = sample_for_output_level();
    return swapped;
  }

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
      outputHigh = compatibilityEnabled();
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
    // A cassette pause is electrical silence after the initial edge.
    sample = 0;
    swapped = advance_read_pos();
    return swapped;
  }
  else {
    i2sWasPauseBlock = false;
    durationUs = workingPeriod ? workingPeriod : 1;
  }

  canShape = true;
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
  config.sample_rate = pcm5120SampleRate;
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
#ifdef PCM5120_I2S_USE_APLL
  config.use_apll = true;
#else
  config.use_apll = false;
#endif
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

void audio_i2s_apply_sample_rate()
{
  if (i2sInstalled) i2s_set_sample_rates(kI2sPort, pcm5120SampleRate);
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
  currentSegmentSamples = 1;
  currentSegmentOffset = 0;
  currentSegmentCanShape = false;
  sampleRemainder = 0;
  stopAfterDrain = false;
  stopAfterDrainDeadline = 0;

  i2s_zero_dma_buffer(kI2sPort);
  i2sRunning = true;
}

void audio_i2s_stop()
{
  if (!i2sInstalled) return;

  // TZXProcess() can reach the logical end of a file while the final
  // generated periods are still in the PCM render buffer.  Drain briefly at
  // natural EOF so the final block is not truncated, then let
  // audio_i2s_service() perform the normal shutdown.  Manual stop remains
  // immediate.
  if (EndOfFile) {
    stopAfterDrain = true;
    stopAfterDrainDeadline = millis() + I2S_EOF_DRAIN_MS;
    return;
  }

  stopAfterDrain = false;
  stopAfterDrainDeadline = 0;
  i2sRunning = false;
  remainingSamples = 0;
  currentSample = 0;
  i2s_zero_dma_buffer(kI2sPort);
}

bool audio_i2s_service()
{
  if (!i2sInstalled || !i2sRunning) return false;

  if (stopAfterDrain && (int32_t)(millis() - stopAfterDrainDeadline) >= 0) {
    stopAfterDrain = false;
    stopAfterDrainDeadline = 0;
    i2sRunning = false;
    remainingSamples = 0;
    currentSample = 0;
    i2s_zero_dma_buffer(kI2sPort);
    return false;
  }

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
      swapped = next_audio_segment(durationUs, nextSample, currentSegmentCanShape) || swapped;
      remainingSamples = us_to_samples(durationUs);
      currentSample = nextSample;
      currentSegmentSamples = remainingSamples;
      currentSegmentOffset = 0;
    }

    uint32_t count = remainingSamples;
    const uint16_t framesLeft = I2S_RENDER_FRAMES - frame;
    if (count > framesLeft) count = framesLeft;

    for(uint32_t i = 0; i < count; ++i) {
      int16_t sample = currentSample;
      if (currentSegmentCanShape && pcm5120Waveform == PCM5120Waveform::SINE) {
        const float phase = (outputHigh ? 0.0f : 3.14159265359f) +
                            3.14159265359f * ((float)currentSegmentOffset / (float)currentSegmentSamples);
        sample = (int16_t)(std::sin(phase) * (float)pcm5120I2sAmplitude);
      } else if (currentSegmentCanShape && pcm5120Waveform == PCM5120Waveform::EDGE_RAMPED) {
        const uint32_t ramp = currentSegmentSamples / 8 > 1 ? currentSegmentSamples / 8 : 1;
        const float t = (float)currentSegmentOffset / (float)ramp;
        const float amount = t < 1.0f ? t : 1.0f;
        const float from = outputHigh ? -1.0f : 1.0f;
        const float to = outputHigh ? 1.0f : -1.0f;
        sample = (int16_t)((from + (to - from) * amount) * (float)pcm5120I2sAmplitude);
      }
      // In the ESP32 legacy I2S driver, RIGHT_LEFT uses the normal I2S
      // slot order: left sample first, right sample second.  Keep the
      // cassette waveform on exactly one slot so the menu selection maps to
      // the physical PCM5120 channel as expected.
      if (pcm5120Channel == PCM5120Channel::LEFT) {
        frames[(frame * 2)] = sample;
        frames[(frame * 2) + 1] = 0;
      } else {
        frames[(frame * 2)] = 0;
        frames[(frame * 2) + 1] = sample;
      }
      frame++;
      currentSegmentOffset++;
    }

    remainingSamples -= count;
  }

  size_t bytesWritten = 0;
  i2s_write(kI2sPort, frames, sizeof(frames), &bytesWritten, portMAX_DELAY);
  return swapped;
}

#endif // defined(USE_PCM5120_I2S) && defined(ESP32)
