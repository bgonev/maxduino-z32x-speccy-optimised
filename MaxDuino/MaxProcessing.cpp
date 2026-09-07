#include "configs.h"
#include "compat.h"
#include "Arduino.h"
#include "CounterPercent.h"
#include "constants.h"
#include "Display.h"
#include "file_utils.h"
#include "isr.h"
#include "casProcessing.h"
#include "buffer.h"
#include "TimerCounter.h"
#include "processing_state.h"
#include "MaxDuino.h" // want to refactor and get rid
#include "MaxProcessing.h"
#include "CheckForExt.h"
#include "current_settings.h"
#include "pinSetup.h"
#include "buttons.h"
#include "utils.h"
#include "audio_i2s.h"
#if defined(USE_PCM5120_I2S) && defined(ESP32)
#include <miniz.h>
#include <cstdlib>
#endif

// submodules
#include "zx8081.h"
#include "ayplay.h"
#ifdef Use_c64
#include "c64tap.h"
#endif
#include "uef.h"
#include "oric.h"
#include "kansas_4b.h"
#ifdef Use_MZF
  #include "mzf.h"
#endif
#ifdef Use_MTX
  #include "mtx.h"
#endif
#ifdef Use_CAQ
  #include "caq.h"
#endif

//Temporarily store for a pulse period before loading it into the buffer.
word currentPeriod=1;

word pauseLength=0;
unsigned long bytesToRead=0;
bool EndOfFile=false;
static const byte TZX_LOOP_STACK_SIZE = 8;
static unsigned long tzxLoopStarts[TZX_LOOP_STACK_SIZE];
static word tzxLoopCounts[TZX_LOOP_STACK_SIZE];
static byte tzxLoopDepth=0;
static unsigned long tzxBlockOffsets[512];
static uint16_t tzxBlockCount=0;
static int tzxCurrentBlock=-1;
struct TzxCallFrame {
  int returnBlock;
  int targets[16];
  byte count;
  byte next;
};
static TzxCallFrame tzxCallStack[8];
static byte tzxCallDepth=0;
static bool tzxInteractiveSelect=true;

static byte chooseTzxSelection(byte count) {
  byte selected=0;
  lastbtn=true;
  while (true) {
    if (button_up() && !lastbtn && selected>0) { selected--; lastbtn=true; }
    if (button_down() && !lastbtn && selected+1<count) { selected++; lastbtn=true; }
    if (button_play() && !lastbtn) { lastbtn=true; return selected; }
    if (button_stop() && !lastbtn) { lastbtn=true; return 0; }
    printtextF(PSTR("Select"), 0);
    utoa((unsigned int)selected + 1, (char *)input, 10);
    printtext((char *)input, 1);
    checkLastButton();
  }
}

static unsigned long tzx_u32(unsigned long p) {
  if (readfile(4, p) != 4) return 0;
  return (unsigned long)filebuffer[0] | ((unsigned long)filebuffer[1] << 8) |
         ((unsigned long)filebuffer[2] << 16) | ((unsigned long)filebuffer[3] << 24);
}

static unsigned long tzx_u24(unsigned long p) {
  if (readfile(3, p) != 3) return 0;
  return (unsigned long)filebuffer[0] | ((unsigned long)filebuffer[1] << 8) |
         ((unsigned long)filebuffer[2] << 16);
}

static unsigned long tzx_block_size(unsigned long p) {
  if (p >= filesize) return 0;
  if (readfile(1, p) != 1) return 0;
  byte id=filebuffer[0];
  if (id==0x10) return 5 + (readfile(2,p+3)==2 ? word(filebuffer[1],filebuffer[0]) : 0);
  if (id==0x11) return 19 + tzx_u24(p+16);
  if (id==0x12) return 5;
  if (id==0x13) return readfile(1,p+1)==1 ? 2 + (2UL*filebuffer[0]) : 0;
  if (id==0x14) return 11 + tzx_u24(p+8);
  if (id==0x15) return 9 + tzx_u24(p+5);
  if (id==0x18 || id==0x19) return 5 + tzx_u32(p+1);
  if (id==0x20) return 3;
  if (id==0x21) return readfile(1,p+1)==1 ? 2 + filebuffer[0] : 0;
  if (id==0x22 || id==0x25 || id==0x27) return 1;
  if (id==0x23 || id==0x24) return 3;
  if (id==0x26) return readfile(2,p+1)==2 ? 3 + 2UL*word(filebuffer[1],filebuffer[0]) : 0;
  if (id==0x28 || id==0x2A) return id==0x2A ? 5 : (readfile(2,p+1)==2 ? 3 + word(filebuffer[1],filebuffer[0]) : 0);
  if (id==0x2B) return 5 + tzx_u32(p+1);
  if (id==0x30) return readfile(1,p+1)==1 ? 2 + filebuffer[0] : 0;
  if (id==0x31) return readfile(1,p+2)==1 ? 3 + filebuffer[1] : 0;
  if (id==0x32) return readfile(2,p+1)==2 ? 3 + word(filebuffer[1],filebuffer[0]) : 0;
  if (id==0x33) return readfile(1,p+1)==1 ? 2 + 3UL*filebuffer[0] : 0;
  if (id==0x35) return 21 + tzx_u32(p+17);
  if (id==0x5A) return 10;
  return 0;
}

static void indexTZXBlocks() {
  tzxBlockCount=0;
  unsigned long p=10;
  while (tzxBlockCount < 512 && p < filesize) {
    unsigned long size=tzx_block_size(p);
    if (!size || size > filesize - p) break;
    tzxBlockOffsets[tzxBlockCount++]=p;
    p += size;
  }
}

PROGMEM const byte TZXTape[7] = {'Z','X','T','a','p','e','!'};

#ifdef ID11CDTspeedup
bool AMScdt = false;
#endif

byte usedBitsInLastByte=8;
word loopCount=0;
byte seqPulses=0;
word temppause=0;
bool forcePause0 = false;
byte currentByte=0;
byte bitChecksum = 0;     // For oric and uef:  0:Even 1:Odd number of one bits  For AY: checksum byte

word pilotPulses=0;
word pilotLength=0;
word sync1Length=0;
word sync2Length=0;
word zeroPulse=0;
word onePulse=0;
unsigned long cswPulsesRemaining=0;
unsigned long cswRunRemaining=0;
word cswSamplePeriodUs=1;
byte cswCompression=0;
#if defined(USE_PCM5120_I2S) && defined(ESP32)
static byte *cswExpandedData = nullptr;
static unsigned long cswExpandedLength = 0;
static unsigned long cswExpandedPos = 0;
#endif
byte uefpassforZero=2;
//byte passforOne=4;
byte jtapflag=255;
byte jpass=0;

word TickToUs(word ticks) {
  // returns (ticks/3.5)+0.5 
  /* Hagen Patzke optimization */
  return (word)((((long(ticks) << 2) + 7) >> 1) / 7);
}

void UniPlay(){
  // initialise scale and period based on current BAUDRATE
  // (although these could be overridden later e.g. during checkForEXT, depending on file type)
  #ifdef Use_CAS
  setCASBaud();
  #endif

  // on entry, currentFile is already pointing to the file entry you want to play
  // and fileName is already set
  if(!entry.open(currentDir, currentFile, O_RDONLY)) {
  //  printtextF(PSTR("Error Opening File"),0);
  }

#ifdef ID11CDTspeedup
  AMScdt = false;
#endif
  block=0;                                    // Initial block when starting
  currentBit=0;                               // fallo reproducción de .tap tras .tzx
  bytesRead=0;                                //start of file
  // Do not let a previous TZX block ID leak into format detection.  TAP and
  // other headerless formats set currentID/currentTask in checkForEXT(); the
  // TZX processor must preserve that decision instead of trying to read a
  // TZX header from the file.
  currentID=BLOCKID::UNKNOWN;
  currentTask=TASK::INIT;                     //
  const char * filenameExt = strrchr(fileName,'.') + 1;
  checkForEXT(filenameExt);
  const bool isTzxFile =
    (filenameExt[0] == 't' || filenameExt[0] == 'T') &&
    (filenameExt[1] == 'z' || filenameExt[1] == 'Z') &&
    (filenameExt[2] == 'x' || filenameExt[2] == 'X') &&
    filenameExt[3] == '\0';
  if (compatibilityMode == CompatibilityMode::AUTO && isTzxFile) {
    bool hasPureTone = false;
    bool hasPulseSequence = false;
    bool hasPureData = false;
    for (uint16_t i = 0; i < tzxBlockCount; ++i) {
      if (readfile(1, tzxBlockOffsets[i]) != 1) continue;
      if (filebuffer[0] == BLOCKID::ID12) hasPureTone = true;
      else if (filebuffer[0] == BLOCKID::ID13) hasPulseSequence = true;
      else if (filebuffer[0] == BLOCKID::ID14) hasPureData = true;
    }
    TSXCONTROLzxpolarityUEFSWITCHPARITY =
      hasPureTone && hasPulseSequence && hasPureData;
  } else {
    TSXCONTROLzxpolarityUEFSWITCHPARITY =
      compatibilityMode == CompatibilityMode::ON;
  }
  isStopped=false;
  
  clearBuffer();

  // for CAS/DRAGON:
#ifdef Use_CAS
  cas_currentType=CAS_TYPE::Nothing;
  fileStage=0;
#endif

  // for TZX/UEF/etc:
  currentBlockTask = BLOCKTASK::READPARAM;    //First block task is to read in parameters
  count_r = 255;                                //End of file buffer flush 
  EndOfFile=false;
  uefpassforZero=2;
  //passforOne=4;
  jtapflag=255;
  tzxLoopDepth=0;
  tzxCallDepth=0;

  reset_output_state();
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  audio_i2s_start();
#else
  Timer.initialize(100000); //100ms pause prevents anything bad happening before we're ready
  Timer.attachInterrupt(wave2);
#endif
}

void UniStop() {
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  audio_i2s_stop();
#else
  Timer.stop();
#endif
  isStopped=true;
  start=0;
  entry.close();                              //Close file
  seekFile(); 
  bytesRead=0;                                // reset read bytes PlayBytes
#ifdef AYPLAY
  AYPASS_hdrptr = AYPASS_STEP::HDRSTART; // reset AY flag
#endif
#ifdef Use_CAS
  casduino = CASDUINO_FILETYPE::NONE;
#endif
  reset_output_state();
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  if (cswExpandedData) {
    std::free(cswExpandedData);
    cswExpandedData = nullptr;
    cswExpandedLength = 0;
    cswExpandedPos = 0;
  }
#endif
}

bool getNextDataByte() {
  if(ReadByte()) {            //Read in a byte
    currentByte = outByte;
    #ifdef AYPLAY
    if (AYPASS_hdrptr == AYPASS_STEP::WRITE_FLAG_BYTE) {
      currentByte = 0xFF;                 // Only insert first DATA byte if sending AY TAP DATA Block and don't decrement counter
      AYPASS_hdrptr = AYPASS_STEP::WRITE_CHECKSUM; // set Checksum flag to be sent when EOF reached
      bytesRead += -1;                    // rollback ptr and compensate for dummy read byte
      bytesToRead += 2;                   // add 2 bytes to read as we send 0xFF (data flag header byte) and chksum at the end of the block
    } else {
    #endif
      bytesToRead += -1;

    if (currentID==BLOCKID::JTAP && jpass == 0) {
      currentByte = jtapflag;
      jpass = 1;
      bytesRead += -1;
      bytesToRead += 1;     
    } 
    
    #ifdef AYPLAY
    }
    bitChecksum ^= currentByte;    // keep calculating checksum
    #endif
    
    if(bytesToRead == 0) {                  //Check for end of data block
      pass = 0;
      bytesRead -= 1;                      //rewind a byte if we've reached the end
      currentBlockTask = BLOCKTASK::PAUSE; // this also handles jumping to GetID if there is no pause (pauseLength==0)
      return false;  // exit
    }
  } else {                         // If we reached the EOF
    
  #ifdef AYPLAY
    // Check if need to send checksum
    if (AYPASS_hdrptr == AYPASS_STEP::WRITE_CHECKSUM)
    {
      currentByte = bitChecksum;          // send calculated chksum
      bytesToRead += 1;                   // add one byte to read
      AYPASS_hdrptr = AYPASS_STEP::FINISHED; // Reset flag to end block
    }
    else
  #endif
    {
      EndOfFile=true;  
      currentBlockTask = BLOCKTASK::PAUSE; // this also handles EOF case when pauseLength==0
      return false;                           // return here if normal TAP or TZX  
    }
  }

  if(bytesToRead!=1) {                      //If we're not reading the last byte play all 8 bits
    currentBit=8;
  } else {
    currentBit=usedBitsInLastByte;          //Otherwise only play back the bits needed
  }

  return true;
}

void writeData() {
  //Convert byte from file into string of pulses.  One pulse per pass
  if(currentBit==0) { //Check for byte end/first byte
    if (!getNextDataByte()) // false == no more bytes
      return;
    pass=0;
  }

  if(currentByte&0x80){                       //Set next period depending on value of bit 0
    currentPeriod = onePulse;
  } else {
    currentPeriod = zeroPulse;
  }
  pass+=1;
  
  if(pass==2) {
    currentByte <<= 1;                        //Shift along to the next bit
    currentBit += -1;
    pass=0;  
  }    
}

void StandardBlock() {
  //Standard Block Playback
  switch (currentBlockTask) {
    case BLOCKTASK::PILOT:
      //Start with Pilot Pulses
      if(!pilotPulses--) {
        currentBlockTask = BLOCKTASK::SYNC1;
      } else {
        currentPeriod = pilotLength;
      }
      break;
    
    case BLOCKTASK::SYNC1:
      //First Sync Pulse
      currentPeriod = sync1Length;
      currentBlockTask = BLOCKTASK::SYNC2;
      break;
    
    case BLOCKTASK::SYNC2:
      //Second Sync Pulse
      currentPeriod = sync2Length;
      currentBlockTask = BLOCKTASK::TDATA;
      break;
    
    case BLOCKTASK::TDATA:
      //Data Playback
#ifdef AYPLAY
      if (currentID==BLOCKID::AYO && AYPASS_hdrptr <= AYPASS_STEP::HDREND)
      {
        WriteAYHeader(); // write TAP Header data from String Vector
      }
      else
      {
        writeData();
      }
#else
      writeData();
#endif
      break;
    
    case BLOCKTASK::PAUSE:
      //Close block with a pause
      if((currentID!=BLOCKID::TAP)&&(currentID!=BLOCKID::JTAP)&&(currentID!=BLOCKID::AYO)) {                  // Check if we have !=AYO too
        temppause = pauseLength;
        currentID = BLOCKID::IDPAUSE;
      } else {
        currentPeriod = pauseLength;
        bitSet(currentPeriod, 15);
        currentBlockTask = BLOCKTASK::READPARAM;
      }

      if(EndOfFile) currentID=BLOCKID::IDEOF;
      break;
  }
}

void PureToneBlock() {
  //Pure Tone Block - Long string of pulses with the same length
  if(!pilotPulses--) {
    currentTask = TASK::GETID;
  } else {
    currentPeriod = pilotLength;
  }
}

void PulseSequenceBlock() {
  //Pulse Sequence Block - String of pulses each with a different length
  //Mainly used in speedload blocks
  if(!seqPulses--) {
    currentTask = TASK::GETID;
  } else {
    if(ReadWord()) {
      currentPeriod = TickToUs(outWord);    
    }
  }
}

void PureDataBlock() {
  //Pure Data Block - Data & pause only, no header, sync
  switch(currentBlockTask) {
    case BLOCKTASK::TDATA:
      writeData();          
    break;
    
    case BLOCKTASK::PAUSE:
      temppause = pauseLength;
      currentID = BLOCKID::IDPAUSE;
    break;
  }
}

static bool readCswEncodedByte(byte &value)
{
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  if (cswCompression == 2) {
    if (cswExpandedPos >= cswExpandedLength) return false;
    value = cswExpandedData[cswExpandedPos++];
    return true;
  }
#endif
  if (bytesToRead == 0 || !ReadByte()) return false;
  bytesToRead--;
  value = outByte;
  return true;
}

#if defined(USE_PCM5120_I2S) && defined(ESP32)
static bool prepareCswZRle(unsigned long compressedLength)
{
  if (compressedLength == 0 || cswPulsesRemaining == 0 ||
      compressedLength > 0xFFFFFFFFUL / 5UL ||
      cswPulsesRemaining > 0xFFFFFFFFUL / 5UL) return false;

  byte *compressed = (byte *)std::malloc(compressedLength);
  const unsigned long capacity = cswPulsesRemaining * 5UL + 1UL;
  byte *expanded = (byte *)std::malloc(capacity);
  if (!compressed || !expanded) {
    std::free(compressed);
    std::free(expanded);
    return false;
  }

  unsigned long position = bytesRead;
  unsigned long remaining = compressedLength;
  unsigned long copied = 0;
  while (remaining) {
    const byte chunk = remaining > 20 ? 20 : (byte)remaining;
    if (readfile(chunk, position) != chunk) {
      std::free(compressed);
      std::free(expanded);
      return false;
    }
    memcpy(compressed + copied, filebuffer, chunk);
    position += chunk;
    copied += chunk;
    remaining -= chunk;
  }

  mz_ulong expandedSize = capacity;
  const int result = mz_uncompress(expanded, &expandedSize, compressed, compressedLength);
  std::free(compressed);
  if (result != MZ_OK || expandedSize == 0) {
    std::free(expanded);
    return false;
  }
  cswExpandedData = expanded;
  cswExpandedLength = expandedSize;
  cswExpandedPos = 0;
  return true;
}
#endif

void CSWBlock() {
  if (currentBlockTask == BLOCKTASK::PAUSE || cswPulsesRemaining == 0) {
#if defined(USE_PCM5120_I2S) && defined(ESP32)
    if (cswExpandedData) {
      std::free(cswExpandedData);
      cswExpandedData = nullptr;
      cswExpandedLength = 0;
      cswExpandedPos = 0;
    }
#endif
    temppause = pauseLength;
    currentID = BLOCKID::IDPAUSE;
    return;
  }

  if (cswRunRemaining == 0) {
    byte encoded = 0;
    if (!readCswEncodedByte(encoded)) { currentID = BLOCKID::IDEOF; return; }
    if (encoded != 0) {
      cswRunRemaining = encoded;
    } else {
      cswRunRemaining = 0;
      for (byte i=0; i<4; ++i) {
        if (!readCswEncodedByte(encoded)) { currentID = BLOCKID::IDEOF; return; }
        cswRunRemaining |= ((unsigned long)encoded << (8 * i));
      }
    }
  }

  unsigned long run = cswRunRemaining;
  const unsigned long maxRun = 65534UL / (cswSamplePeriodUs ? cswSamplePeriodUs : 1);
  if (maxRun && run > maxRun) run = maxRun;
  currentPeriod = word(run * cswSamplePeriodUs);
  cswRunRemaining -= run;
  cswPulsesRemaining--;
}

void writeDataDirect() {
  // Push byte from file into the buffer, with minimal processing.
  // One byte (8 bits) from file turns directly into one entry in the buffer
  // that encapsulates the DirectRecording mode (plus an extra entry at the
  // start to indicate the sample period))
  if (!getNextDataByte()) // false == no more bytes
  {
    currentPeriod = 0;
    return;
  }

  // 010xxiiibbbbbbbb = 0x4000 + (I<<8) + B
  // |||\/\_/\______/
  // ||| | |    |
  // ||| | |    +-- B = 8 bits (leftmost bit next)
  // ||| | +-- I = 0..7 = number of remaining b bits (minus 1). i.e. 7 means 'use all 8 bits', 0 means 'just use one bit'
  // ||| +-- X = unused
  // ||+-- always 0
  // |+-- always 1
  // +-- always 0

  // process 8 bits (or, all currentBit #bits), in one go, to reduce overhead here
  currentPeriod = (1<<14) + ((currentBit-1)<<8) + currentByte;
}

void writeDataDirect16() {
  // Push byte from file into the buffer, with even less processing.
  // This is equivalent to call writeDataDirect 8 times (which generates
  // 8 words i.e. 16 bytes in the wbuffer)
  // This actually bypasses several passes of the TZXLoop / TZXProcess steps
  // This will only be called if there are > 8 more bytes still to read AND
  // space in the output buffer to write 16 bytes without hitting buffsize
  for(byte iter=8; iter>0; --iter)
  {
    if (!getNextDataByte()) // false == no more bytes
    {
      currentPeriod = 0;
      return;
    }

    // process 8 bits in one go
    // 010xxiiibbbbbbbb = 0x4000 + (I<<8) + B
    //      \_/ <-- I = 0..7 = number of remaining b bits (minus 1). i.e. 7 means 'use all 8 bits'
    // (I must be 7 because we already checked we're not on the last byte)
    //
    // so:
    //     currentPeriod = (1<<14) + (7<<8) + currentByte;
    // and then:
    //     writeBuffer[writeppos] = currentPeriod /256;   //add period to the buffer
    //     writeBuffer[writepos+1] = currentPeriod %256;   //add period to the buffer
    //
    // but we don't even need to use currentPeriod variable for this, we can do directly:
    // Also note that the following code takes as much as possible out of the noInterrupts section;
    // on Nano328p there are only 2 assembly instructions between noInterrupts() and interrupts())
    // 5e7a:	f8 94       	cli
    // 5e7c:	20 82       	st	Z, r2
    // 5e7e:	81 83       	std	Z+1, r24	; 0x01
    // 5e80:	78 94       	sei

    const byte _b1 = currentByte;
    volatile byte * _wb = writeBuffer+writepos;
    noInterrupts();                       //Pause interrupts while we add a period to the buffer
    *_wb = 0x47; // = ((1<<14) + (7<<8))>>8
    *(_wb+1) = _b1;
    interrupts();
    writepos+=2;
  }
}


void ForcePauseAfter0() {
  pauseOn=true;
  printtext2F(PSTR("PAUSED* "),0);
  forcePause0=false;
  return;  
}

void UniSetup() {
  INIT_OUTPORT;
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  audio_i2s_begin();
#endif
  isStopped=true;
  pinState=LOW;
  WRITE_LOW;
}

void HeaderFail() {
  printtextF(PSTR("Not Valid File"), 0);
  delay(300);     
  UniStop();
}

void ReadTZXHeader() {
  //Read and check first 10 bytes for a TZX header
  if(readfile(10, 0)==10 && memcmp_P(filebuffer, TZXTape, 7)==0) {
    bytesRead = 10;
    indexTZXBlocks();
    return;
  }

  HeaderFail();
}

void TZXProcess() {
  if(currentBlockTask==BLOCKTASK::ID15_TDATA)
  {
    // shortcut for ID15 handler for performance
    writeDataDirect();
    return;
  }

  currentPeriod = 0;
  switch (currentTask) {
    case TASK::INIT:
      // Headerless formats are selected by checkForEXT().  Only an unknown
      // format needs the TZX header reader.
      if (currentID == BLOCKID::UNKNOWN) {
        currentTask = TASK::GETFILEHEADER;
      } else {
        currentTask = TASK::PROCESSID;
      }
      break;
  
    case TASK::GETFILEHEADER:
      //grab 7 byte string
      ReadTZXHeader();
      //set current task to GETID
      currentTask = TASK::GETID;
      break;

  #ifdef AYPLAY
    case TASK::GETAYHEADER:
      //grab 8 byte string
      ReadAYHeader();
      //set current task to PROCESSID
      currentTask = TASK::PROCESSID;
      break;
  #endif

#ifdef Use_UEF
    case TASK::GETUEFHEADER:
      //grab 12 byte string
      ReadUEFHeader();
      //set current task to GETCHUNKID
      currentTask = TASK::GETCHUNKID;
      break;

    case TASK::GETCHUNKID:
      tzx_process_taskid_uef_getchunkid();
      break;

    case TASK::PROCESSCHUNKID:
      tzx_process_taskid_uef_processchunkid();
      break;
#endif // Use_UEF
    
    case TASK::GETID:
      //grab 1 byte ID
      if(ReadByte()) {
        currentID = outByte;
        tzxCurrentBlock=-1;
        for (uint16_t i=0; i<tzxBlockCount; ++i) {
          if (tzxBlockOffsets[i] == bytesRead-1) { tzxCurrentBlock=i; break; }
        }
      } else {
        // There is no block after the final data block.  Mark the logical
        // end here so the PCM renderer can drain the already generated final
        // buffer without adding the legacy synthetic EOF pulse train.
        EndOfFile = true;
        currentID = BLOCKID::IDEOF;
      }
      //reset data block values
      currentBit=0;
      pass=0;
      //set current task to PROCESSID
      currentTask = TASK::PROCESSID;
      currentBlockTask = BLOCKTASK::READPARAM;
      break;

    case TASK::PROCESSID:
      //ID Processing
      switch(currentID) {
        case BLOCKID::ID10:
          //Process ID10 - Standard Block
          switch (currentBlockTask) {
            case BLOCKTASK::READPARAM:
              block_mem_oled();
              if(ReadWord()) {
                pauseLength = outWord;
              }
              if(ReadWord()) {
                bytesToRead = outWord +1;
              }
              if(ReadByte()) {
                if(outByte == 0) {
                  pilotPulses = PILOTNUMBERL;
                } else {
                  pilotPulses = PILOTNUMBERH;
                }
                bytesRead += -1;
              }
              pilotLength = PILOTLENGTH;
              sync1Length = SYNCFIRST;
              sync2Length = SYNCSECOND;
              zeroPulse = ZEROPULSE;
              onePulse = ONEPULSE;
              currentBlockTask = BLOCKTASK::PILOT;
              usedBitsInLastByte=8;
              break;
            
            default:
              StandardBlock();
              break;
          }
          break;
      
        case BLOCKID::ID11:
          //Process ID11 - Turbo Tape Block
          switch (currentBlockTask) {
            case BLOCKTASK::READPARAM:
              block_mem_oled();
                          
            #if not defined(ID11CDTspeedup)
              if(ReadWord()) {
                pilotLength = TickToUs(outWord);
              }
              if(ReadWord()) {
                sync1Length = TickToUs(outWord);
              }
              if(ReadWord()) {
                sync2Length = TickToUs(outWord);
              }          
              if(ReadWord()) {
                zeroPulse = TickToUs(outWord);
              }
              if(ReadWord()) {
                onePulse = TickToUs(outWord);
              }          
            #else    
              if (compatibilityEnabled() && AMScdt){ 
                bytesRead += 10;
                switch(BAUDRATE){
                  case 1200: // 1000 Normal baudrate
                    //zeroPulse = TickToUs(1167);
                    pilotLength = onePulse = 666;
                    sync1Length = sync2Length = zeroPulse = 333;                                           
                    break;
                  case 2400: // 2000 High baudrate
                    //zeroPulse = TickToUs(583);
                    pilotLength = onePulse = 334;
                    sync1Length = sync2Length = zeroPulse = 167; 
                    break;
                  case 3150:
                  case 3600: // 3500 Max ROM baudrate
                    //zeroPulse = TickToUs(333);
                    pilotLength = onePulse = 190;
                    sync1Length = sync2Length = zeroPulse = 95;                    
                    break;
                  case 3850: // 4000 Specvar loader baudrate
                    //zeroPulse = TickToUs(292);
                    pilotLength = onePulse = 166;
                    sync1Length = sync2Length = zeroPulse = 83;                      
                    break;
                }                                                                                                   
              } else {
                if(ReadWord()) {
                  pilotLength = TickToUs(outWord);
                }
                if(ReadWord()) {
                  sync1Length = TickToUs(outWord);
                }
                if(ReadWord()) {
                  sync2Length = TickToUs(outWord);
                }                             
                if(ReadWord()) {
                  zeroPulse = TickToUs(outWord);
                }
                if(ReadWord()) {
                  onePulse = TickToUs(outWord);
                }
              }    
            #endif
                            
              if(ReadWord()) {
                pilotPulses = outWord;
              }
              if(ReadByte()) {
                usedBitsInLastByte = outByte;
              }
              if(ReadWord()) {
                pauseLength = outWord;
              }
              if(ReadLong()) {
                bytesToRead = outLong +1;
              }
              currentBlockTask = BLOCKTASK::PILOT;
            break;
          
          default:
            StandardBlock();
            break;
          }
          break; // case ID11

        case BLOCKID::ID12:
          //Process ID12 - Pure Tone Block      
          if(currentBlockTask==BLOCKTASK::READPARAM){
            if(ReadWord()) {
                pilotLength = TickToUs(outWord);
            }
            if(ReadWord()) {
              pilotPulses = outWord;
              //DebugBlock("Pilot Pulses", pilotPulses);
            }
            currentBlockTask = BLOCKTASK::PILOT;
          } else {
            PureToneBlock();
          }
          break;

        case BLOCKID::ID13:
          //Process ID13 - Sequence of Pulses                 
          if(currentBlockTask==BLOCKTASK::READPARAM) {  
            if(ReadByte()) {
              seqPulses = outByte;
            }
            currentBlockTask = BLOCKTASK::TDATA;
          } else {
            PulseSequenceBlock();
          }
          break;

        case BLOCKID::ID14:
          //process ID14 - Pure Data Block             
          if(currentBlockTask==BLOCKTASK::READPARAM) {
            if(ReadWord()) {
              zeroPulse = TickToUs(outWord); 
            }
            if(ReadWord()) {
              onePulse = TickToUs(outWord); 
            }
            if(ReadByte()) {
              usedBitsInLastByte = outByte;
            }
            if(ReadWord()) {
              pauseLength = outWord; 
            }
            if(ReadLong()) {
              bytesToRead = outLong+1;
            }
            currentBlockTask=BLOCKTASK::TDATA;
          } else {
            PureDataBlock();
          }
          break;

      case BLOCKID::ID15:
        //process ID15 - Direct Recording          
        if(currentBlockTask==BLOCKTASK::READPARAM) {
          block_mem_oled();
          currentBit = 0;
          unsigned long SampleLength=0;
          if(ReadWord()) {     
            SampleLength = TickToUs(outWord);
          }
          if(ReadWord()) {      
            //Pause after this block in milliseconds
            pauseLength = outWord;  
          }
          if(ReadByte()) {
            //Used bits in last byte (other bits should be 0)
            usedBitsInLastByte = outByte;
          }
          if(ReadLong()) {
            // Length of samples' data
            bytesToRead = outLong+1;
            // Uncomment next line for testing to force id error
            //currentID=BLOCKID::UNKNOWN;
          }            
          currentBlockTask=BLOCKTASK::ID15_TDATA;

            // and write the sample period information to the output using this format:
            // 011sssssssssssss  = 0x6000 + S
            // |||\___________/
            // |||     |
            // |||     +-- S = sampleLength
            // ||+-- always 1
            // |+-- always 1
            // +-- always 0

            currentPeriod = SampleLength | 0x6000;

          } else if(currentBlockTask==BLOCKTASK::PAUSE) {
            temppause = pauseLength;
            currentID = BLOCKID::IDPAUSE;                     
          } else {
            writeDataDirect();
          }
          break;


        case BLOCKID::ID18:
          if (currentBlockTask == BLOCKTASK::READPARAM) {
            unsigned long blockLength = 0;
            if (ReadDword()) blockLength = outLong;
            if (ReadWord()) pauseLength = outWord;
            unsigned long sampleRate = 0;
            for (byte i=0; i<3; ++i) {
              if (!ReadByte()) { currentID = BLOCKID::IDEOF; break; }
              sampleRate |= ((unsigned long)outByte << (8 * i));
            }
            if (ReadByte()) cswCompression = outByte;
            if (ReadDword()) cswPulsesRemaining = outLong;
            if (blockLength < 10 || sampleRate == 0 || (cswCompression != 1 && cswCompression != 2)) {
              currentID = BLOCKID::UNKNOWN;
              break;
            }
            cswSamplePeriodUs = (word)(1000000UL / sampleRate);
            if (cswSamplePeriodUs == 0) cswSamplePeriodUs = 1;
            bytesToRead = blockLength - 10;
            cswRunRemaining = 0;
#if defined(USE_PCM5120_I2S) && defined(ESP32)
            if (cswCompression == 2) {
              if (!prepareCswZRle(bytesToRead)) {
                currentID = BLOCKID::UNKNOWN;
                break;
              }
              bytesRead += bytesToRead;
              bytesToRead = 0;
            }
#endif
            currentBlockTask = BLOCKTASK::TDATA;
          } else {
            CSWBlock();
          }
          break;

        case BLOCKID::ID19:
          //Process ID19 - Generalized data block
          switch (currentBlockTask) {
            case BLOCKTASK::READPARAM:
              #ifdef BLOCKID19_IN      
                block_mem_oled();
              #endif

              if(ReadDword()) {
                #ifdef BLOCKID19_IN
                  bytesToRead = outLong;
                #endif
              }
              if(ReadWord()) {
                //Pause after this block in milliseconds
                pauseLength = outWord;
              }

              bytesRead += 86 ; // skip until DataStream filename
              #ifdef BLOCKID19_IN
                bytesToRead += -88; // pauseLength + SYMDEFs
              #endif
              //currentBlockTask=PAUSE;
              currentBlockTask=BLOCKTASK::TDATA;
              break;
          /*
            case PAUSE:
              currentPeriod = PAUSELENGTH;
              bitSet(currentPeriod, 15);
              currentBlockTask=BLOCKTASK::TDATA;
              break; 
          */               
            case BLOCKTASK::TDATA:
              ZX8081DataBlock();
              break;
          }  
          break; // case ID19

        case BLOCKID::ID20:
          //process ID20 - Pause Block          
          if(ReadWord()) {
            if(outWord>0) {
              forcePause0=false;          // pause0 FALSE
              temppause = outWord;
            } else {                    // If Pause duration is 0 ms then Stop The Tape
              forcePause0=true;          // pause0 TRUE
            }
            currentID = BLOCKID::IDPAUSE;         
          }
          break;

        case BLOCKID::ID21:
          //Process ID21 - Group Start
          #if defined(BLOCKID21_IN)
            block_mem_oled();
          #endif
                        
          if(ReadByte()) {
            bytesRead += outByte;
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID22:
          //Process ID22 - Group End          
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID23: {
          if (!ReadWord() || tzxCurrentBlock < 0) { currentID=BLOCKID::UNKNOWN; break; }
          int target = tzxCurrentBlock + 1 + (int)(int16_t)outWord;
          if (target < 0 || target >= tzxBlockCount) { currentID=BLOCKID::UNKNOWN; break; }
          bytesRead = tzxBlockOffsets[target];
          currentTask = TASK::GETID;
          break;
        }

        case BLOCKID::ID24:
          //Process ID24 - Loop Start          
          if(ReadWord() && outWord > 0 && tzxLoopDepth < TZX_LOOP_STACK_SIZE) {
            tzxLoopCounts[tzxLoopDepth] = outWord;
            tzxLoopStarts[tzxLoopDepth] = bytesRead;
            tzxLoopDepth++;
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID25:
          //Process ID25 - Loop End          
          if (tzxLoopDepth > 0) {
            if (--tzxLoopCounts[tzxLoopDepth - 1] != 0) {
              bytesRead = tzxLoopStarts[tzxLoopDepth - 1];
            } else {
              tzxLoopDepth--;
            }
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID26: {
          if (!ReadWord() || tzxCurrentBlock < 0 || outWord == 0 || outWord > 16 || tzxCallDepth >= 8) { currentID=BLOCKID::UNKNOWN; break; }
          TzxCallFrame &frame = tzxCallStack[tzxCallDepth++];
          frame.returnBlock = tzxCurrentBlock + 1;
          frame.count=(byte)outWord;
          frame.next=1;
          for (byte i=0; i<frame.count; ++i) {
            if (!ReadWord()) { currentID=BLOCKID::UNKNOWN; break; }
            frame.targets[i]=tzxCurrentBlock + 1 + (int)(int16_t)outWord;
            if (frame.targets[i] < 0 || frame.targets[i] >= tzxBlockCount) { currentID=BLOCKID::UNKNOWN; break; }
          }
          int target = frame.targets[0];
          if (target < 0 || target >= tzxBlockCount) { currentID=BLOCKID::UNKNOWN; break; }
          bytesRead = tzxBlockOffsets[target];
          currentTask = TASK::GETID;
          break;
        }

        case BLOCKID::ID27:
          if (tzxCallDepth == 0) { currentID=BLOCKID::UNKNOWN; break; }
          {
          TzxCallFrame &frame = tzxCallStack[tzxCallDepth - 1];
          if (frame.next < frame.count) {
            int target=frame.targets[frame.next++];
            bytesRead=tzxBlockOffsets[target];
          } else if (frame.returnBlock >= 0 && frame.returnBlock < tzxBlockCount) {
            bytesRead=tzxBlockOffsets[frame.returnBlock];
            tzxCallDepth--;
          } else { currentID=BLOCKID::UNKNOWN; break; }
          currentTask = TASK::GETID;
          }
          break;

        case BLOCKID::ID28: {
          unsigned int length=0;
          if (!ReadWord() || !ReadByte() || tzxCurrentBlock < 0) { currentID=BLOCKID::UNKNOWN; break; }
          length = outWord;
          byte selections = outByte;
          if (selections == 0) { currentID=BLOCKID::UNKNOWN; break; }
          static int selectionsTargets[32];
          if (selections > 32) { currentID=BLOCKID::UNKNOWN; break; }
          for (byte i=0; i<selections; ++i) {
            if (!ReadWord()) { currentID=BLOCKID::UNKNOWN; break; }
            selectionsTargets[i]=tzxCurrentBlock + 1 + (int)(int16_t)outWord;
            if (!ReadByte()) { currentID=BLOCKID::UNKNOWN; break; }
            bytesRead += outByte;
          }
          byte selected = tzxInteractiveSelect ? chooseTzxSelection(selections) : 0;
          int target = selectionsTargets[selected];
          if (target < 0 || target >= tzxBlockCount) { currentID=BLOCKID::UNKNOWN; break; }
          bytesRead = tzxBlockOffsets[target];
          currentTask = TASK::GETID;
          (void)length;
          break;
        }

        case BLOCKID::ID2A:
          //Skip//        
          bytesRead+=4;
          if (skip2A) currentTask = TASK::GETID;
          else {
            forcePause0 = true;
            currentID = BLOCKID::IDPAUSE;
          }        
          break;

        case BLOCKID::ID2B:
          // Set signal level. The block payload is a DWORD length followed
          // by a level byte; current TZX files use a one-byte payload.
          {
            unsigned long blockLength = 0;
            if (!ReadDword() || blockLength < 1 || !ReadByte()) {
              currentID = BLOCKID::UNKNOWN;
              break;
            }
            if (blockLength > 1) {
              if (blockLength - 1 > filesize - bytesRead) {
                currentID = BLOCKID::UNKNOWN;
                break;
              }
              bytesRead += blockLength - 1;
            }
            currentPeriod = LEVEL_SIGNAL_FLAG | (outByte ? 1 : 0);
            currentTask = TASK::GETID;
          }
          break;
        
        case BLOCKID::ID30:
          //Process ID30 - Text Description         
          if(ReadByte()) {
            //Show info on screen - removed until bigger screen used
            //byte j = outByte;
            //for(byte i=0; i<j; i++) {
            //  if(ReadByte()) {
            //    lcd.print(char(outByte));
            //  }
            //}
            bytesRead += outByte;
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID31:
          //Process ID31 - Message block         
          if(ReadByte()) {
            // dispayTime = outByte;
          }         
          if(ReadByte()) {
            bytesRead += outByte;
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID32:
          //Process ID32 - Archive Info
          //Block Skipped until larger screen used          
          if(ReadWord()) {
            bytesRead += outWord;
          }
          currentTask = TASK::GETID;
          break;

        case BLOCKID::ID33:
          //Process ID33 - Machine Info
          //Block Skipped until larger screen used         
          if(ReadByte()) {
            bytesRead += (long(outByte) * 3);
          }
          currentTask = TASK::GETID;
          break;       

        case BLOCKID::ID35:
          //Process ID35 - Custom Info Block
          //Block Skipped          
          bytesRead += 0x10;
          if(ReadDword()) {
            bytesRead += outLong;
          }
          currentTask = TASK::GETID;
          break;
        
        case BLOCKID::ID4B:
          tzx_process_blockid_kansas_4b();
          break; // Case_ID4B

        case BLOCKID::ID5A:
          // Glue block; nothing to do (skip it)
          bytesRead += 9;
          currentTask = TASK::GETID;
          break;

      #ifdef Use_CAQ
        case BLOCKID::CAQ:
          caq_process();
          break;
      #endif

      #ifdef Use_MZF
        case BLOCKID::MZF:
          mzf_process();
          break;
      #endif

      #ifdef Use_MTX
        case BLOCKID::MTX:
          mtx_process();
          break;
      #endif

        case BLOCKID::JTAP:
      /*    //Jupiter Tap file block
          switch(currentBlockTask) {                 
            case BLOCKTASK::READPARAM:
              #if defined(BLOCKTAP_IN)
                block_mem_oled();
              #endif
              
              pauseLength = 1000;
              if(ReadWord()) {
                bytesToRead = outWord+1;
              }

              jpass = 0; 
              jtapflag ^= 0xFF;
              if(jtapflag == 0) {
                pilotPulses = 4096 + 1; //SP:8063 //JP:4096
              } else {
                pilotPulses = 512 + 1; //SP:3223 //JP:512
              }

              pilotLength = 575; //SP:2168T=2168/3.5=619u //JP:2011T=575u
              sync1Length = 172; //SP:667T=191u //JP:601T=172u
              sync2Length = 226; //SP:735T=210u //JP:791T=226u
              zeroPulse = 229;  //SP:855T=244u //JP:800T=229u
              onePulse = 457;   //SP:1710T=489u //JP:1600T=457u
              currentBlockTask = BLOCKTASK::PILOT;
              usedBitsInLastByte=8;
              break;

            default:
              StandardBlock();
              break;
          }
          break; // Case JTAP
     */   
        case BLOCKID::TAP:
          //Pure Tap file block
          switch(currentBlockTask) {
            case BLOCKTASK::READPARAM:

              #if defined(BLOCKTAP_IN)
                block_mem_oled();
              #endif
                  
              pauseLength = PAUSELENGTH;
              if(ReadWord()) {
                bytesToRead = outWord+1;
              }

              switch(currentID) {

                case BLOCKID::JTAP:
                  jpass = 0; 
                  jtapflag ^= 0xFF;
                  if(jtapflag == 0) {
                    pilotPulses = 4096 + 1; //SP:8063 //JP:4096
                  } else {
                    pilotPulses = 512 + 1; //SP:3223 //JP:512
                  }
    
                  pilotLength = 575; //SP:2168T=2168/3.5=619u //JP:2011T=575u
                  sync1Length = 172; //SP:667T=191u //JP:601T=172u
                  sync2Length = 226; //SP:735T=210u //JP:791T=226u
                  zeroPulse = 229;  //SP:855T=244u //JP:800T=229u
                  onePulse = 457;   //SP:1710T=489u //JP:1600T=457u
                  currentBlockTask = BLOCKTASK::PILOT;
                  usedBitsInLastByte=8;             
                  break;

                case BLOCKID::TAP:
                default:
                
                  if(ReadByte()) {
                    if(outByte == 0) {
                      pilotPulses = PILOTNUMBERL + 1;
                    } else {
                      pilotPulses = PILOTNUMBERH + 1;
                    }
                    bytesRead += -1;
                  }
                  pilotLength = PILOTLENGTH;
                  sync1Length = SYNCFIRST;
                  sync2Length = SYNCSECOND;
                  zeroPulse = ZEROPULSE;
                  onePulse = ONEPULSE;
                  currentBlockTask = BLOCKTASK::PILOT;
                  usedBitsInLastByte=8;               
                  break;                                 
              }              
              break; // for BLOCKTASK::READPARAM

            default:
              StandardBlock();
              break;
          }
          break; // Case for combined BLOCKID TAP & JTAP

      #ifdef Use_c64
        case BLOCKID::C64TAP:
          c64tap_process();
          break;
      #endif

        case BLOCKID::ZXP:
          tzx_process_blockid_zx8081_zxp();
          break; // Case ZXP
        
        case BLOCKID::ZXO:
          tzx_process_blockid_zx8081_zxo();
          break; // Case ZXO
        
      #ifdef AYPLAY
        case BLOCKID::AYO:                           //AY File - Pure AY file block - no header, must emulate it
          switch(currentBlockTask) {
            case BLOCKTASK::READPARAM:
              pauseLength = PAUSELENGTH;  // Standard 1 sec pause
                                          // here we must generate the TAP header which in pure AY files is missing.
                                          // This was done with a DOS utility called FILE2TAP which does not work under recent 32bit OSs (only using DOSBOX).
                                          // TAPed AY files begin with a standard 0x13 0x00 header (0x13 bytes to follow) and contain the 
                                          // name of the AY file (max 10 bytes) which we will display as "ZXAYFile " followed by the 
                                          // length of the block (word), checksum plus 0xFF to indicate next block is DATA.
                                          // 13 00[00 03(5A 58 41 59 46 49 4C 45 2E 49)1A 0B 00 C0 00 80]21<->[1C 0B FF<AYFILE>CHK]
              pilotLength = PILOTLENGTH;
              sync1Length = SYNCFIRST;
              sync2Length = SYNCSECOND;
              zeroPulse = ZEROPULSE;
              onePulse = ONEPULSE;
              currentBlockTask = BLOCKTASK::PILOT;    // now send pilot, SYNC1, SYNC2 and TDATA (writeheader() from String Vector on 1st pass then writeData() on second)
              bitChecksum = 0;
              bytesRead = 0;
              if (AYPASS_hdrptr == AYPASS_STEP::HDRSTART){
                pilotPulses = PILOTNUMBERL + 1;
              } else {
                // already sent header, so now send data block (shorter pilot)
                pilotPulses = PILOTNUMBERH + 1;
                bytesToRead = filesize+5;   // set length of file to be read plus data byte and CHKSUM (and 2 block LEN bytes)
              }
              usedBitsInLastByte=8;
              break;

            default:
              StandardBlock();
              break;
          }  
          break; // Case AYO
      #endif

      #ifdef tapORIC
        case BLOCKID::ORIC:
          tzx_process_blockid_oric();
          break;
      #endif // tapORIC
                
        case BLOCKID::IDPAUSE:
          if(temppause>0) {
            if(temppause > MAXPAUSE_PERIOD) {
              currentPeriod = MAXPAUSE_PERIOD;
              temppause += -MAXPAUSE_PERIOD;    
            } else {
              currentPeriod = temppause;
              temppause = 0;
            }
            bitSet(currentPeriod, 15);
          } else {
            if (forcePause0) { // Stop the Tape
              if(!count_r==0) {
                currentPeriod = 32769;
                count_r += -1;
              } else {
                currentTask = TASK::GETID;
                count_r = 255;
                ForcePauseAfter0();
              }
            } else { 
              currentTask = TASK::GETID;
              if(EndOfFile) currentID=BLOCKID::IDEOF;
            }
          } 
          break;
    
        case BLOCKID::IDEOF:
          //Handle end of file
          if(!count_r==0) {
            currentPeriod = 10;
            bitSet(currentPeriod, 15);
            bitSet(currentPeriod, 13);
            count_r += -1;
          } else {
            stopFile();
            return;
          }       
          break; 
        
        default:
          //ID Not Recognised - Fall back if non TZX file or unrecognised ID occurs
          
          #ifdef LCDSCREEN16x2
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("ID? ");
            lcd.setCursor(4,0);
            //lcd.print(String(currentID, HEX));
            ultoa(currentID,PlayBytes,16);
            lcd.print(PlayBytes);
            lcd.setCursor(0,1);
            //lcd.print(String(bytesRead,HEX) + " - L: " + String(loopCount, DEC));
            ultoa(bytesRead,PlayBytes,16);
            lcd.print(PlayBytes) ;  lcd.print(" - L: "); lcd.print(loopCount);
          #endif

          #ifdef OLED1306
            ultoa(bytesRead,PlayBytes,16);
            printtext(PlayBytes,lineaxy);

          #endif 
          
          #ifdef P8544             
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("ID? ");
            lcd.setCursor(4,0);
            //lcd.print(String(currentID, HEX));
            ultoa(currentID,PlayBytes,16);
            lcd.print(PlayBytes);
            lcd.setCursor(0,1);
            //lcd.print(String(bytesRead,HEX) + " - L: " + String(loopCount, DEC));
            ultoa(bytesRead,PlayBytes,16);
            lcd.print(PlayBytes) ;  lcd.print(" - L: "); lcd.print(loopCount);
          #endif

          noInterrupts();  
          while(!button_stop()) {
            //waits until the button Stop is pressed.
            //delay(50);
          }
          interrupts();
          stopFile();
          break;
      
      } // end of CurrentID switch statement
      break;
  } // end of CurrentTask switch statement
}

void TZXLoop() {   
  if(currentBlockTask == BLOCKTASK::ID15_TDATA && writepos+16<=buffsize && bytesToRead>=16)
  {
    // shortcut for ID15 handler for performance
    // write 8 input bytes (=16 output bytes to buffer)
    // ALSO: skips the lcd updates (SHOW_CNTR, SHOW_PCT) entirely
    writeDataDirect16();
    return;
  }

  if(writepos<buffsize){                    // Keep filling until full
    TZXProcess();                           //generate the next period to add to the buffer
    if(currentPeriod>0) {
      //add period to the buffer
      const byte _b1 = currentPeriod /256;
      const byte _b2 = currentPeriod %256;
      volatile byte * _wb = writeBuffer+writepos;
      noInterrupts();                       //Pause interrupts while we add a period to the buffer
      *_wb = _b1;
      *(_wb+1) = _b2;
      interrupts();
      writepos+=2;
    }
  } else {
    if (!pauseOn) {
    #if defined(SHOW_CNTR)
      lcdTime();          
    #endif
    #if defined(SHOW_PCT)          
      lcdPercent();
    #endif
    }
  } 
}

void UniLoop() {
#if defined(USE_PCM5120_I2S) && defined(ESP32)
  for(byte i=0; i<64 && writepos<buffsize && start==1; i++)
  {
  #ifdef Use_CAS
    if (casduino!=CASDUINO_FILETYPE::NONE)
    {
      casduinoLoop();
    }
    else
  #endif
    {
      TZXLoop();
    }
  }

  if(audio_i2s_service())
  {
    writepos=0;
  }
#else
  bool _copybuff;
  noInterrupts();
  //Pause interrupts to prevent var reads and copy values out
  isStopped = pauseOn;
  _copybuff = morebuff;
  morebuff = false;
  interrupts();

  if(_copybuff)
  {
    //Buffer has swapped, start from the beginning of the new page
    writepos=0;
  }

 #ifdef Use_CAS
    if (casduino!=CASDUINO_FILETYPE::NONE)
    {
      casduinoLoop();  
    }
    else
 #endif
    {
      TZXLoop();
    }
#endif
}
