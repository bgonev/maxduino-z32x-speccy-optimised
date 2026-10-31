#include "configs.h"
#include "file_utils.h"
#include "sdfat_config.h"
#include <SdFat.h>
#include <stdlib.h>

SdBaseFile entry;  // SD card file
unsigned long bytesRead=0;
unsigned long filesize;
byte lastByte;
_readout_type readout;

byte filebuffer[20]; // used for small reads from files (readfile, ReadByte, etc use this), sized for the largest header read
static byte *playbackCache = nullptr;
static unsigned long playbackCacheSize = 0;

// Keep the complete tape image in RAM when it is small enough.  This removes
// SD latency from the timing-sensitive playback path while leaving very large
// images on the normal streaming path.
static const unsigned long PLAYBACK_CACHE_LIMIT = 128UL * 1024UL;

bool preparePlaybackCache()
{
  releasePlaybackCache();
  if (filesize == 0 || filesize > PLAYBACK_CACHE_LIMIT) return false;
  byte *cache = (byte *)malloc(filesize);
  if (!cache) return false;
  if (!entry.seekSet(0) || entry.read(cache, filesize) != (int)filesize) {
    free(cache);
    return false;
  }
  playbackCache = cache;
  playbackCacheSize = filesize;
  return true;
}

void releasePlaybackCache()
{
  if (playbackCache) free(playbackCache);
  playbackCache = nullptr;
  playbackCacheSize = 0;
}

byte readfile(byte nbytes, unsigned long p)
{
  byte i=0;
  // Never allow a malformed tape image to read past the end of the file.
  if (p <= filesize && nbytes <= (filesize - p) &&
      playbackCache && p <= playbackCacheSize && nbytes <= (playbackCacheSize - p)) {
    memcpy(filebuffer, playbackCache + p, nbytes);
    i = nbytes;
  } else if (p <= filesize && nbytes <= (filesize - p) && entry.seekSet(p)) {
    i=entry.read(filebuffer, nbytes);
  } 
  return i;
}

byte ReadByte() {
  //Read a byte from the file, and move file position on one if successful
  //Always reads from bytesRead, which is the current position in the file
  if(readfile(1, bytesRead)==1)
  {
    bytesRead += 1;
    outByte = filebuffer[0];
    return true;
  }
  return false;
}

byte ReadWord() {
  //Read 2 bytes from the file, and move file position on two if successful
  //Always reads from bytesRead, which is the current position in the file
  if(readfile(2, bytesRead)==2)
  {
    bytesRead += 2;
    outWord = word(filebuffer[1], filebuffer[0]);
    return true;
  }
  return false;
}

byte ReadLong() {
  //Read 3 bytes from the file, and move file position on three if successful
  //Always reads from bytesRead, which is the current position in the file
  if(readfile(3, bytesRead)==3)
  {
    bytesRead += 3;
    outLong = ((unsigned long) word(filebuffer[2], filebuffer[1]) << 8) | filebuffer[0];
    return true;
  }
  return false;
}

byte ReadDword() {
  //Read 4 bytes from the file, and move file position on four if successful  
  //Always reads from bytesRead, which is the current position in the file
  if(readfile(4, bytesRead)==4)
  {
    bytesRead += 4;
    outLong = ((unsigned long)word(filebuffer[3], filebuffer[2]) << 16) | word(filebuffer[1], filebuffer[0]);
    return true;
  }
  return false;
}
