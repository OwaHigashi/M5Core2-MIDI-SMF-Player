// 尾和東@Pococha技術枠
// necobit版SMFプレーヤーをUNIT-SYNTHを装着したCore2で演奏するように改造

#include <M5Core2.h>
#include <SD.h>
#include "SmfSeq.h"
#include "MidiPort.h"
#include "MidiFunc.h"
#include "SmfFileAccess.h"
#include "IntervalCheck.h"
#include "common.h"

// SD-Updater support
#include "M5StackUpdater.h"

// Playlist configuration
#define MAX_SONGS 100
#define MAX_FILENAME_LENGTH 64
#define SMF_FOLDER "/smf"

// MIDI Serial port configuration
#define MIDI_SERIAL Serial2

// Global variables
char songFilenames[MAX_SONGS][MAX_FILENAME_LENGTH];
int songCount = 0;
int currentSongIndex = 0;
char *currentFilename = NULL;

SMF_SEQ_TABLE *pseqTbl = NULL;

// Timer variables
hw_timer_t *sysTimer = NULL;
volatile bool timerFlag = false;

// Display refresh interval
IntervalCheck displayRefresh(100, true);  // 100ms refresh

// =============================================================================
// MIDI Port Implementation
// =============================================================================

int MidiPort_open()
{
  MIDI_SERIAL.begin(D_MIDI_PORT_BPS, SERIAL_8N1, -1, 32);  // Core2 MIDI output on pin 32
  return 0;
}

void MidiPort_close()
{
  MIDI_SERIAL.end();
}

int MidiPort_write(UCHAR data)
{
  MIDI_SERIAL.write(data);
  return 0;
}

int MidiPort_writeBuffer(UCHAR *pData, ULONG Len)
{
  MIDI_SERIAL.write(pData, Len);
  return 0;
}

// =============================================================================
// MIDI Function Implementation
// =============================================================================

int midiOutOpen()
{
  return MidiPort_open();
}

int midiOutClose()
{
  MidiPort_close();
  return MIDI_OK;
}

int midiOutShortMsg(UCHAR status, UCHAR data1, UCHAR data2)
{
  UCHAR buf[3] = {status, data1, data2};
  return MidiPort_writeBuffer(buf, 3) == 3 ? MIDI_OK : MIDI_NG;
}

int midiOutLongMsg(UCHAR *Buf, ULONG Len)
{
  return MidiPort_writeBuffer(Buf, Len) == Len ? MIDI_OK : MIDI_NG;
}

int midiOutGMReset()
{
  // GM Reset: Control Change on all channels
  UCHAR buf[3];
  for (int ch = 0; ch < 16; ch++) {
    buf[0] = MIDI_STATCH_CTRLCHG | ch;
    buf[1] = 0x7E;  // Non-registered parameter
    buf[2] = 0x00;
    MidiPort_writeBuffer(buf, 3);
  }
  delay(100);
  return MIDI_OK;
}

// =============================================================================
// SMF File Access Implementation
// =============================================================================

static File smfFile;

bool SmfFileAccessOpen(UCHAR *Filename)
{
  smfFile = SD.open((const char *)Filename, FILE_READ);
  return smfFile ? true : false;
}

void SmfFileAccessClose()
{
  if (smfFile) {
    smfFile.close();
  }
}

bool SmfFileAccessRead(UCHAR *Buf, unsigned long Ptr)
{
  if (!smfFile) return false;

  if (!smfFile.seek(Ptr)) return false;

  int data = smfFile.read();
  if (data < 0) return false;

  *Buf = (UCHAR)data;
  return true;
}

bool SmfFileAccessReadNext(UCHAR *Buf)
{
  if (!smfFile) return false;

  int data = smfFile.read();
  if (data < 0) return false;

  *Buf = (UCHAR)data;
  return true;
}

int SmfFileAccessReadBuf(UCHAR *Buf, unsigned long Ptr, int Lng)
{
  if (!smfFile) return 0;

  if (!smfFile.seek(Ptr)) return 0;

  return smfFile.read(Buf, Lng);
}

unsigned int SmfFileAccessSize()
{
  if (!smfFile) return 0;
  return smfFile.size();
}

// =============================================================================
// Song Playlist Management
// =============================================================================

void scanSongs()
{
  File dir = SD.open(SMF_FOLDER);
  if (!dir || !dir.isDirectory()) {
    M5.Lcd.print("No /smf folder");
    return;
  }

  songCount = 0;
  File file = dir.openNextFile();

  while (file && songCount < MAX_SONGS) {
    if (!file.isDirectory()) {
      const char *name = file.name();
      if (name) {
        // Check file extension
        const char *ext = strrchr(name, '.');
        if (ext && (strcasecmp(ext, ".mid") == 0 || strcasecmp(ext, ".smf") == 0)) {
          // Build full path
          snprintf(songFilenames[songCount], MAX_FILENAME_LENGTH,
                   "%s/%s", SMF_FOLDER, name);
          songCount++;
        }
      }
    }
    file.close();
    file = dir.openNextFile();
  }

  dir.close();

  // Sort filenames alphabetically (simple bubble sort)
  for (int i = 0; i < songCount - 1; i++) {
    for (int j = 0; j < songCount - i - 1; j++) {
      if (strcmp(songFilenames[j], songFilenames[j + 1]) > 0) {
        char temp[MAX_FILENAME_LENGTH];
        strcpy(temp, songFilenames[j]);
        strcpy(songFilenames[j], songFilenames[j + 1]);
        strcpy(songFilenames[j + 1], temp);
      }
    }
  }
}

char *makeFilename(int offset)
{
  currentSongIndex = (currentSongIndex + offset) % songCount;
  if (currentSongIndex < 0) {
    currentSongIndex = songCount + currentSongIndex;
  }

  if (songCount > 0) {
    currentFilename = songFilenames[currentSongIndex];
    return currentFilename;
  }

  return NULL;
}

const char *getDisplayFileName(const char *fullPath)
{
  if (!fullPath) return "";

  const char *filename = strrchr(fullPath, '/');
  if (filename) {
    return filename + 1;
  }
  return fullPath;
}

// =============================================================================
// Timer ISR for SMF Sequencing
// =============================================================================

void IRAM_ATTR timerISR()
{
  timerFlag = true;
}

void setupTimer()
{
  // Use timer 0, prescaler 80 (80MHz / 80 = 1MHz), count up
  // For 30ms interval: 1MHz / (1000000 / 30) = 30000 counts
  sysTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(sysTimer, &timerISR, true);
  timerAlarmWrite(sysTimer, ZTICK * 1000, true);  // ZTICK ms in microseconds
  timerAlarmEnable(sysTimer);
}

void stopTimer()
{
  if (sysTimer) {
    timerAlarmDisable(sysTimer);
    timerDetachInterrupt(sysTimer);
    timerEnd(sysTimer);
    sysTimer = NULL;
  }
}

// =============================================================================
// Display Functions
// =============================================================================

void drawBackground()
{
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("M5Core2 SMF Player");

  // Draw button labels at bottom
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Lcd.setCursor(20, 300);
  M5.Lcd.print("PREV");
  M5.Lcd.setCursor(130, 300);
  M5.Lcd.print("PLAY/STOP");
  M5.Lcd.setCursor(260, 300);
  M5.Lcd.print("NEXT");
}

void updateDisplay()
{
  if (!displayRefresh.check()) {
    return;
  }

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Lcd.fillRect(0, 20, 320, 40, TFT_BLACK);
  M5.Lcd.setCursor(0, 20);

  if (songCount > 0) {
    M5.Lcd.printf("Song: %d/%d", currentSongIndex + 1, songCount);
  } else {
    M5.Lcd.print("No songs found");
  }

  // Display filename
  M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Lcd.fillRect(0, 65, 320, 30, TFT_BLACK);
  M5.Lcd.setCursor(0, 65);

  const char *displayName = getDisplayFileName(currentFilename);
  if (displayName) {
    M5.Lcd.printf("%.30s", displayName);
  } else {
    M5.Lcd.print("No file");
  }

  // Display playback status
  M5.Lcd.fillRect(0, 100, 320, 30, TFT_BLACK);
  M5.Lcd.setCursor(0, 100);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  if (pseqTbl) {
    switch (SmfSeqGetStatus(pseqTbl)) {
      case SMF_STAT_PLAY:
        M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Lcd.print("Status: PLAYING");
        break;
      case SMF_STAT_PAUSE:
        M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Lcd.print("Status: PAUSED");
        break;
      case SMF_STAT_STOP:
        M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
        M5.Lcd.print("Status: STOPPED");
        break;
      default:
        M5.Lcd.print("Status: UNKNOWN");
        break;
    }
  }

  // Keyboard visualization area (reserved for drawKey calls)
  M5.Lcd.fillRect(0, 140, 320, 140, TFT_DARKGREY);
}

// =============================================================================
// Button Handling
// =============================================================================

void handleButtonPress()
{
  M5.update();

  // Button A - Previous song
  if (M5.BtnA.wasPressed()) {
    if (pseqTbl && songCount > 0) {
      SmfSeqStop(pseqTbl);
      char *fname = makeFilename(-1);
      if (fname) {
        SmfSeqFileLoad(pseqTbl, fname);
      }
      displayRefresh.reset();
    }
  }

  // Button B - Play/Stop
  if (M5.BtnB.wasPressed()) {
    if (pseqTbl && songCount > 0) {
      int status = SmfSeqGetStatus(pseqTbl);
      if (status == SMF_STAT_STOP) {
        if (currentFilename == NULL) {
          currentFilename = makeFilename(0);
          SmfSeqFileLoad(pseqTbl, currentFilename);
        }
        SmfSeqStart(pseqTbl);
      } else if (status == SMF_STAT_PLAY) {
        SmfSeqStop(pseqTbl);
      } else if (status == SMF_STAT_PAUSE) {
        SmfSeqPauseRelease(pseqTbl);
      }
      displayRefresh.reset();
    }
  }

  // Button C - Next song
  if (M5.BtnC.wasPressed()) {
    if (pseqTbl && songCount > 0) {
      SmfSeqStop(pseqTbl);
      char *fname = makeFilename(1);
      if (fname) {
        SmfSeqFileLoad(pseqTbl, fname);
      }
      displayRefresh.reset();
    }
  }
}

// =============================================================================
// Setup
// =============================================================================

void setup()
{
  M5.begin();
  M5.Lcd.setRotation(0);

  // Initialize SD card
  if (!SD.begin(4, SPI, 25000000)) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.print("SD Card init failed!");
    while (1) delay(100);
  }

  // Scan for songs
  drawBackground();
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(0, 150);
  M5.Lcd.print("Scanning for songs...");

  scanSongs();

  M5.Lcd.fillRect(0, 150, 320, 30, TFT_BLACK);
  M5.Lcd.setCursor(0, 150);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.printf("Found %d songs", songCount);
  delay(1000);

  // Initialize SMF sequencer
  pseqTbl = SmfSeqInit(ZTICK);

  if (!pseqTbl) {
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setCursor(0, 200);
    M5.Lcd.print("SMF Init failed!");
    while (1) delay(100);
  }

  // Load first song if available
  if (songCount > 0) {
    currentFilename = makeFilename(0);
    SmfSeqFileLoad(pseqTbl, currentFilename);
  }

  // Setup timer for SMF processing
  setupTimer();

  drawBackground();
  updateDisplay();
}

// =============================================================================
// Main Loop
// =============================================================================

void loop()
{
  // Handle button presses
  handleButtonPress();

  // Process SMF sequencing on timer tick
  if (timerFlag) {
    timerFlag = false;

    if (pseqTbl && SmfSeqGetStatus(pseqTbl) == SMF_STAT_PLAY) {
      SmfSeqTickProc(pseqTbl);

      // Check if song has finished
      if (SmfSeqGetStatus(pseqTbl) == SMF_STAT_STOP) {
        if (songCount > 0) {
          // Auto-advance to next song
          char *fname = makeFilename(1);
          if (fname) {
            SmfSeqFileLoad(pseqTbl, fname);
            SmfSeqStart(pseqTbl);
          }
        }
        displayRefresh.reset();
      }
    }
  }

  // Update display
  updateDisplay();

  delay(1);
}
