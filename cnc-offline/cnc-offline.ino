#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

// Pin Definitions
#define upPin       PA0
#define downPin     PA1
#define selectPin   PA2
#define cancelPin   PA3
#define SD_card_Reader PA4

#define DEBOUNCE_DELAY 50
#define SAFE_Z_HEIGHT  10.0   // mm — raised before XY go-to-zero

// LCD Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Global State Variables
byte currentMenu = 0;
byte selectedOption = 0;
bool fileRunning = false;
bool filePaused = false;

// File Progress Variables
bool awaitingOK = false;
String currentRunningFile = "";
int numLineTotal = 0;
int numLineAct = 0;
int numLineSent = 0;
bool allLinesSent = false;
bool jobCompleted = false;
bool isFileLine = false;

// Feed Override Variables
unsigned long lastFeedOverrideTime = 0;
bool showFeedOverrideMessage = false;
String feedOverrideMsg = "";
int currentFeedRate = 100;

static bool mpgEnabled = true;

// Step Sizes
float selectedXYStep = 10.0;
float selectedZStep = 5.0;

const char* mainMenuOptions[] = {
  "Jog", "Unlock", "ZeroXYZ", "Run Files", "ZeroX", "ZeroY", "ZeroZ", "Steps", "Home", "Status", "Reset"
};

const byte mainMenuOptionCount = 11;

String lastDisplayLine0 = "";
String lastDisplayLine1 = "";
unsigned long lastDisplayUpdate = 0;
#define DISPLAY_UPDATE_INTERVAL 500

// ─── Progress tracking (used during file run) ────────────────────────────────
static int runProgressPct = 0;

void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(upPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);
  pinMode(cancelPin, INPUT_PULLUP);

  Serial.begin(115200);

  if (!SD.begin(SD_card_Reader)) {
    lcd.setCursor(0, 0);
    lcd.print(F("No SD Card"));
    while (1);
  }

  Serial.write(0x18); // Ctrl+X soft reset
  Serial.flush();
  delay(1500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F(" Harinder CNC"));
  delay(1500);

  showMainMenu();
}

void sendCodeLine(const String &code) {
  if (code.length() == 0) return;

  Serial.println(code);
  Serial.flush();

  awaitingOK = true;
  unsigned long timeout = millis();

  while (awaitingOK && (millis() - timeout < 1000)) {
    checkForOK();

    if (fileRunning) {
      checkFeedOverrideButtons();
      if (checkCancelButton()) return;
      if (checkPauseButton()) {
        updatePauseDisplay(currentRunningFile, runProgressPct);
      }
    }
  }

  awaitingOK = false;
}

void checkForOK() {
  static char rxBuf[128];
  static uint8_t idx = 0;

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (idx == 0) continue;

      rxBuf[idx] = '\0';
      idx = 0;

      if (strcmp(rxBuf, "ok") == 0) {
        awaitingOK = false;
        continue;
      }

      if (strncmp(rxBuf, "error", 5) == 0) {
        awaitingOK = false;

        if (!fileRunning) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("GRBL Error"));
          lcd.setCursor(0, 1);
          lcd.print(rxBuf);
          delay(2000);
        }
        continue;
      }

      if (strstr(rxBuf, "Pgm End") || strstr(rxBuf, "Program End")) {
        if (fileRunning) {
          jobCompleted = true;
        }
        continue;
      }

      if (rxBuf[0] == '<' && strstr(rxBuf, "Idle")) {
        if (fileRunning && allLinesSent) {
          jobCompleted = true;
        }
        continue;
      }
    }
    else {
      if (idx < sizeof(rxBuf) - 1) {
        rxBuf[idx++] = c;
      }
    }
  }
}

void updatePauseDisplay(String filename, int pct) {
  if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }
  lastDisplayUpdate = millis();
  
  String line0 = "";
  String line1 = "";
  
  if (filePaused) {
    line0 = "PAUSED          ";
    line1 = "SEL:Res CAN:Stop";
  } else {
    if (showFeedOverrideMessage && (millis() - lastFeedOverrideTime < 1500)) {
      line0 = feedOverrideMsg;
      while (line0.length() < 16) line0 += " ";
    } else {
      showFeedOverrideMessage = false;
      char row0[17];
      snprintf(row0, sizeof(row0), "Running: %3d%%", pct);
      line0 = row0;
    }
    line1 = filename.substring(0, 16);
    while (line1.length() < 16) line1 += " ";
  }
  
  if (line0 != lastDisplayLine0) {
    lcd.setCursor(0, 0);
    lcd.print(line0);
    lastDisplayLine0 = line0;
  }
  
  if (line1 != lastDisplayLine1) {
    lcd.setCursor(0, 1);
    lcd.print(line1);
    lastDisplayLine1 = line1;
  }
}

void showMainMenu() {
  currentMenu = 0;
  selectedOption = 0;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(">")); lcd.print(mainMenuOptions[0]);
  lcd.setCursor(0, 1); lcd.print(F(" ")); lcd.print(mainMenuOptions[1]);
}

void updateMainMenu() {
  lcd.clear();
  byte startOption = selectedOption > 0 ? selectedOption - 1 : 0;
  for (byte i = 0; i < 2; i++) {
    lcd.setCursor(0, i);
    byte currentOption = startOption + i;
    if (currentOption < mainMenuOptionCount) {
      lcd.print(currentOption == selectedOption ? ">" : " ");
      lcd.print(F(""));
      lcd.print(mainMenuOptions[currentOption]);
    }
  }
}

void showFileMenu() {
  currentMenu = 1;
  selectedOption = 0;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(">Back"));
  String filename = getFileName(1);
  if (filename != "") {
    lcd.setCursor(0, 1); lcd.print(F(" "));
    lcd.print(filename.substring(0, 14));
  } else {
    lcd.setCursor(0, 1); lcd.print(F(" No files"));
  }
}

void updateFileMenu() {
  lcd.clear();
  byte totalFiles = getFileCount();

  if (totalFiles == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Back"));
    lcd.setCursor(0, 1); lcd.print(F(" No files"));
    return;
  }

  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Back"));
    String firstFile = getFileName(1);
    if (firstFile != "") {
      lcd.setCursor(0, 1); lcd.print(F(" "));
      lcd.print(firstFile.substring(0, 14));
    }
  } else {
    String currentFile = getFileName(selectedOption);
    String nextFile = (selectedOption < totalFiles) ? getFileName(selectedOption + 1) : "";

    if (currentFile != "") {
      lcd.setCursor(0, 0);
      lcd.print(F(">"));
      lcd.print(currentFile.substring(0, 14));
    }

    if (nextFile != "") {
      lcd.setCursor(0, 1);
      lcd.print(F(" "));
      lcd.print(nextFile.substring(0, 14));
    }
  }
}

String getFileName(byte index) {
  byte count = 0;
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      root.close();
      return "";
    }
    if (!entry.isDirectory()) {
      count++;
      if (count == index) {
        String result = entry.name();
        entry.close(); 
        root.close();
        return result;
      }
    }
    entry.close();
  }
}

byte getFileCount() {
  byte count = 0;
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      root.close();
      return count;
    }
    if (!entry.isDirectory()) {
      count++;
    }
    entry.close();
  }
}

bool checkCancelButton() {
  static unsigned long lastCancelCheck = 0;
  if (millis() - lastCancelCheck > 50) {
    if (digitalRead(cancelPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(cancelPin) == LOW) {
        while (digitalRead(cancelPin) == LOW) {}
        Serial.println("\x18"); // Send Ctrl+X to reset GRBL
        fileRunning = false;
        filePaused = false;
        allLinesSent = false;
        jobCompleted = false;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Job Cancelled"));
        delay(2000);
        showMainMenu();
        return true;
      }
    }
    lastCancelCheck = millis();
  }
  return false;
}

bool checkPauseButton() {
  static unsigned long lastPauseCheck = 0;
  if (millis() - lastPauseCheck > 50) {
    if (digitalRead(selectPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(selectPin) == LOW) {
        while (digitalRead(selectPin) == LOW) {}
        filePaused = !filePaused;
        if (filePaused) {
          Serial.println("!"); // Send Feed Hold
        } else {
          Serial.println("~"); // Send Resume
        }
        return true;
      }
    }
    lastPauseCheck = millis();
  }
  return false;
}

void checkFeedOverrideButtons() {
  if (!fileRunning || filePaused) return;

  if (digitalRead(upPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(upPin) == LOW) {
      while (digitalRead(upPin) == LOW) {}
      Serial.println("\x91"); // GRBL command for Feed Override +10%
      if (currentFeedRate < 200) {
          currentFeedRate += 10;
      }
      feedOverrideMsg = "Feed " + String(currentFeedRate) + "%";
      showFeedOverrideMessage = true;
      lastFeedOverrideTime = millis();
    }
  }

  if (digitalRead(downPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(downPin) == LOW) {
      while (digitalRead(downPin) == LOW) {}
      Serial.println("\x92"); // GRBL command for Feed Override -10%
      if (currentFeedRate > 10) {
          currentFeedRate -= 10;
      }
      feedOverrideMsg = "Feed " + String(currentFeedRate) + "%";
      showFeedOverrideMessage = true;
      lastFeedOverrideTime = millis();
    }
  }
}

void clearSerialBuffer() {
  while (Serial.available()) {
    Serial.read();
    delay(1);
  }
}

void sendFile(String filename) {
  File dataFile = SD.open(filename);
  if (!dataFile) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("File Error!"));
    delay(2000);
    return;
  }

  unsigned long jobStartTime = 0;
  unsigned long fileSize = dataFile.size();

  // Reset feed override to 100%
  Serial.write(0x90);
  delay(80);

  // Reset state
  fileRunning = true;
  filePaused = false;
  allLinesSent = false;
  jobCompleted = false;
  currentFeedRate = 100;
  currentRunningFile = filename;
  awaitingOK = false;
  runProgressPct = 0;

  jobStartTime = millis();

  lastDisplayLine0 = "";
  lastDisplayLine1 = "";
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Running:   0%"));
  lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));
  delay(300);

  const int MAX_QUEUE = 35;

  int queued = 0;

  // Initial safe commands
  sendCodeLine("G90");
  sendCodeLine("G21");

  clearSerialBuffer();

  while (fileRunning && !jobCompleted) {
    // Update progress percentage
    if (fileSize > 0 && !allLinesSent) {
      runProgressPct = (int)((dataFile.position() * 100UL) / fileSize);
      if (runProgressPct > 99) runProgressPct = 99;
    }
    if (allLinesSent) runProgressPct = 100;

    checkFeedOverrideButtons();
    if (checkCancelButton()) {
      dataFile.close();
      return;
    }
    if (checkPauseButton()) {
      updatePauseDisplay(filename, runProgressPct);
    }

    if (!filePaused) {
      while (queued < MAX_QUEUE && dataFile.available() && !allLinesSent) {
        String line = readNextGcodeLine(dataFile);
        if (line.length() > 0) {
          Serial.println(line);
          Serial.flush();
          queued++;
        }
      }

      checkForOK();

      if (!awaitingOK && queued > 0) {
        queued--;
        awaitingOK = (queued > 0);
      }

      if (!dataFile.available()) {
        allLinesSent = true;
        dataFile.close();
      }
    }

    if (allLinesSent && !jobCompleted) {
      static unsigned long lastPoll = 0;
      if (millis() - lastPoll > 300) {
        Serial.println("?");
        lastPoll = millis();
      }
    }

    updatePauseDisplay(filename, runProgressPct);
  }

  // Finished
  lcd.clear();

  unsigned long elapsed = (millis() - jobStartTime) / 1000;
  unsigned long hours = elapsed / 3600;
  unsigned long minutes = (elapsed % 3600) / 60;
  unsigned long seconds = elapsed % 60;

  lcd.setCursor(0, 0);
  lcd.print(F("Completed 100%"));

  lcd.setCursor(0, 1);
  lcd.print(F("Time: "));
  lcd.print(hours); lcd.print(F("h "));
  lcd.print(minutes); lcd.print(F("m "));
  lcd.print(seconds); lcd.print(F("s"));

  delay(4000);

  dataFile.close();
  fileRunning = false;
  filePaused = false;
  allLinesSent = false;
  jobCompleted = false;
  currentRunningFile = "";
  showMainMenu();
}

String readNextGcodeLine(File &file) {
  String line;
  line.reserve(48);

  while (file.available()) {
    char c = file.read();
    if (c == '\n') break;
    if (c != '\r') line += c;
  }

  line.trim();

  if (line.length() == 0 || line.startsWith(";") || line.startsWith("(")) {
    return "";
  }

  return line;
}

void jog(String axis, float distance, int feed) {
  Serial.println("$J=G91 " + axis + String(distance, 2) + " F" + String(feed));
}

void showJogMenu() {
  currentMenu = 2;
  selectedOption = 0;
  lcd.clear();
  updateJogMenu();
}

void updateJogMenu() {
  lcd.clear();
  const char* options[] = {
    "Back", "X+", "X-", "Y+", "Y-", "Z+", "Z-",  
    "GoZero", "E/D MPG"
  };
  const byte optionCount = 9;

  byte startOption = selectedOption;
  if (startOption > 0) startOption--;

  for (byte i = 0; i < 2; i++) {
    lcd.setCursor(0, i);
    byte currentOptionIndex = startOption + i;
    if (currentOptionIndex < optionCount) {
      lcd.print((currentOptionIndex == selectedOption) ? ">" : " ");
      lcd.print(options[currentOptionIndex]);
      if (currentOptionIndex >= 1 && currentOptionIndex <= 6) {
        lcd.setCursor(8, i);
        lcd.print(currentOptionIndex <= 4 ? String(selectedXYStep, 1) + "mm" : String(selectedZStep, 1) + "mm");
      }
    }
  }
}

void showStepsMenu() {
  currentMenu = 3;
  selectedOption = 0;
  lcd.clear();
  updateStepsMenu();
}

void updateStepsMenu() {
  lcd.clear();
  const char* options[] = {
    "Back", "X/Y 10mm", "X/Y 50mm", "Z 0.1mm", "Z 1mm", "Z 5mm"
  };
  const byte optionCount = 6;

  byte startOption = selectedOption;
  if (startOption > 0) startOption--;

  for (byte i = 0; i < 2; i++) {
    lcd.setCursor(0, i);
    byte currentOptionIndex = startOption + i;
    if (currentOptionIndex < optionCount) {
      lcd.print((currentOptionIndex == selectedOption) ? ">" : " ");
      lcd.print(options[currentOptionIndex]);
    }
  }
}

void handleStepsMenu() {
  if (digitalRead(upPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(upPin) == LOW) {
      if (selectedOption > 0) selectedOption--;
      updateStepsMenu();
      while (digitalRead(upPin) == LOW) {}
    }
  }
  if (digitalRead(downPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(downPin) == LOW) {
      if (selectedOption < 5) selectedOption++;
      updateStepsMenu();
      while (digitalRead(downPin) == LOW) {}
    }
  }
  if (digitalRead(selectPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(selectPin) == LOW) {
      while (digitalRead(selectPin) == LOW) {}
      switch (selectedOption) {
        case 0:
          showMainMenu();
          return;
        case 1:
          selectedXYStep = 10.0;
          lcd.clear(); lcd.print(F("X/Y Step: 10mm"));
          delay(1000);
          break;
        case 2:
          selectedXYStep = 50.0;
          lcd.clear(); lcd.print(F("X/Y Step: 50mm"));
          delay(1000);
          break;
        case 3:
          selectedZStep = 0.1;
          lcd.clear(); lcd.print(F("Z Step: 0.1mm"));
          delay(1000);
          break;
        case 4:
          selectedZStep = 1.0;
          lcd.clear(); lcd.print(F("Z Step: 1mm"));
          delay(1000);
          break;
        case 5:
          selectedZStep = 5.0;
          lcd.clear(); lcd.print(F("Z Step: 5mm"));
          delay(1000);
          break;
      }
      updateStepsMenu();
    }
  }
}

void handleJogMenu() {
  if (digitalRead(upPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(upPin) == LOW) {
      if (selectedOption > 0) selectedOption--;
      updateJogMenu();
      while (digitalRead(upPin) == LOW) {}
    }
  }
  if (digitalRead(downPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(downPin) == LOW) {
      if (selectedOption < 8) selectedOption++;
      updateJogMenu();
      while (digitalRead(downPin) == LOW) {}
    }
  }
  if (digitalRead(selectPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(selectPin) == LOW) {
      while (digitalRead(selectPin) == LOW) {}
      switch (selectedOption) {
        case 0:
          showMainMenu();
          return;
        case 1: jog("X", selectedXYStep, 1000); break;
        case 2: jog("X", -selectedXYStep, 1000); break;
        case 3: jog("Y", selectedXYStep, 1000); break;
        case 4: jog("Y", -selectedXYStep, 1000); break;
        case 5: jog("Z", selectedZStep, 300); break;
        case 6: jog("Z", -selectedZStep, 300); break;
        case 7: // Go to Zero — raise Z first for safety
          lcd.clear(); lcd.print("Raise Z...");
          Serial.println("G90");
          Serial.println("G0 Z" + String(SAFE_Z_HEIGHT, 1));
          delay(1500);
          lcd.clear(); lcd.print("Go to Zero...");
          Serial.println("G0 X0 Y0");
          delay(1500);
          Serial.println("G0 Z0");
          delay(1000);
          break;
        case 8: // E/D MPG toggle
          mpgEnabled = !mpgEnabled;
          lcd.clear();
          if (mpgEnabled) {
            lcd.print("MPG Enabled");
            Serial.write(0x8B);
          } else {
            lcd.print("MPG Disabled");
            Serial.write(0x8B);
          }
          delay(1000);
          break;
      }
      delay(200);
      updateJogMenu();
    }
  }
}

void showStatusScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checking...");

  Serial.println("?");
  Serial.flush();

  char response[64];
  int idx = 0;
  unsigned long timeout = millis();

  while (millis() - timeout < 1000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;
      if (idx < 63) response[idx++] = c;
    }
  }
  response[idx] = '\0';

  lcd.clear();
  if (idx > 0) {
    lcd.setCursor(0, 0);
    for (int i = 0; i < 16 && i < idx; i++) {
      lcd.write(response[i]);
    }
    if (idx > 16) {
      lcd.setCursor(0, 1);
      for (int i = 16; i < 32 && i < idx; i++) {
        lcd.write(response[i]);
      }
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("No Response");
  }

  delay(3000);
  showMainMenu();
}

void loop() {
  static unsigned long lastButtonTime = 0;

  if (currentMenu == 2) {
    handleJogMenu();
    return;
  }

  if (currentMenu == 3) {
    handleStepsMenu();
    return;
  }

  if (millis() - lastButtonTime > 200) {
    if (digitalRead(upPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(upPin) == LOW) {
        lastButtonTime = millis();
        if (currentMenu == 0 && selectedOption > 0) {
          selectedOption--;
          updateMainMenu();
        } else if (currentMenu == 1 && selectedOption > 0) {
          selectedOption--;
          updateFileMenu();
        }
        while (digitalRead(upPin) == LOW) {}
      }
    }

    if (digitalRead(downPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(downPin) == LOW) {
        lastButtonTime = millis();
        if (currentMenu == 0 && selectedOption < (mainMenuOptionCount - 1)) {
          selectedOption++;
          updateMainMenu();
        } else if (currentMenu == 1) {
          byte maxFiles = getFileCount();
          if (selectedOption < maxFiles) {
            selectedOption++;
            updateFileMenu();
          }
        }
        while (digitalRead(downPin) == LOW) {}
      }
    }

    if (digitalRead(selectPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(selectPin) == LOW) {
        lastButtonTime = millis();
        if (currentMenu == 0) {
          switch (selectedOption) {
            case 0: showJogMenu(); break;
            case 1:
              lcd.clear();
              lcd.print(F("Unlocking..."));
              sendCodeLine("$X");
              delay(1500);
              showMainMenu();
              break;
            case 2:
              lcd.clear();
              lcd.print(F("Zero XYZ..."));
              sendCodeLine("G10 P0 L20 X0 Y0 Z0");
              delay(1500);
              showMainMenu();
              break;
            case 3: showFileMenu(); break;
            case 4:
              lcd.clear();
              lcd.print(F("Zero X..."));
              sendCodeLine("G10 P0 L20 X0");
              delay(1500);
              showMainMenu();
              break;
            case 5:
              lcd.clear();
              lcd.print(F("Zero Y..."));
              sendCodeLine("G10 P0 L20 Y0");
              delay(1500);
              showMainMenu();
              break;
            case 6:
              lcd.clear();
              lcd.print(F("Zero Z..."));
              sendCodeLine("G10 P0 L20 Z0");
              delay(1500);
              showMainMenu();
              break;
            case 7: showStepsMenu(); break;
            case 8:
              lcd.clear();
              lcd.print(F("Homing..."));
              sendCodeLine("$H");
              delay(1500);
              showMainMenu();
              break;
            case 9:
              showStatusScreen();
              break;
            case 10: // Reset
              lcd.clear();
              lcd.print(F("Resetting..."));
              Serial.write(0x18);
              Serial.flush();
              delay(2000);
              showMainMenu();
              break;
          }
        } else if (currentMenu == 1) {
          if (selectedOption == 0) {
            showMainMenu();
          } else {
            String filename = getFileName(selectedOption);
            if (filename != "") {
              sendFile(filename);
            }
          }
        }
        while (digitalRead(selectPin) == LOW) {}
      }
    }
  }
}
