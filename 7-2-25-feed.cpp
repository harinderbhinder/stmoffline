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

// LCD Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Global State Variables
bool awaitingOK = false;
unsigned int numLineTotal = 0, numLineAct = 0;
byte currentMenu = 0;
byte selectedOption = 0;
bool fileRunning = false;
bool filePaused = false;

// Feed Override Variables
unsigned long lastFeedOverrideTime = 0;
bool showFeedOverrideMessage = false;
String feedOverrideMsg = "";

// ADDED: Global variable to hold the current filename for display purposes
String currentRunningFile = "";

// Forward declaration for sendCodeLine
void sendCodeLine(String code);

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);

  pinMode(upPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);
  pinMode(cancelPin, INPUT_PULLUP);

  Serial.begin(115200);

  if (!SD.begin(SD_card_Reader)) {
    lcd.setCursor(0, 0);
    lcd.print(F("Card Error!"));
    while (1);
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(" Harinder"));
  delay(2000);

  sendCodeLine("$10=0");
  showMainMenu();
}

void showMainMenu() {
  currentMenu = 0;
  selectedOption = 0;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(">Unlock "));
  lcd.setCursor(0, 1); lcd.print(F(" ZeroXYZ"));
}

void updateMainMenu() {
  lcd.clear();
  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(" ZeroXYZ"));
  } else if (selectedOption == 1) {
    lcd.setCursor(0, 0); lcd.print(F(" Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroXYZ"));
  } else if (selectedOption == 2) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroXYZ"));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroX"));
  } else if (selectedOption == 3) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroX"));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroY"));
  } else if (selectedOption == 4) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroY"));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroZ"));
  } else if (selectedOption == 5) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroZ"));
    lcd.setCursor(0, 1); lcd.print(F(">Run Files"));
  } else if (selectedOption == 6) {
    lcd.setCursor(0, 0); lcd.print(F(" Run Files"));
    lcd.setCursor(0, 1); lcd.print(F(">Jog"));
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
        entry.close(); root.close();
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


// MODIFIED: This function is completely rewritten for a better user experience.
// It now shows the feed override message on the top line while keeping the
// progress information on the bottom line.
void updatePauseDisplay(String filename, int progress) {
  lcd.clear();

  // Handle the top line of the display
  if (showFeedOverrideMessage && (millis() - lastFeedOverrideTime < 1500)) {
    // Show feed override message for 1.5 seconds
    lcd.setCursor(0, 0);
    lcd.print(feedOverrideMsg);
  } else {
    // After the message timeout, reset the flag
    showFeedOverrideMessage = false;
    // Show the normal status on the top line
    if (filePaused) {
      lcd.setCursor(0, 0); lcd.print(F("PAUSED"));
      lcd.setCursor(7, 0); lcd.print(progress); lcd.print(F("%"));
    } else {
      lcd.setCursor(0, 0); lcd.print(F("Running:"));
    }
  }

  // Handle the bottom line of the display
  if (filePaused) {
    lcd.setCursor(0, 1); lcd.print(F("SEL:Resume CAN:Stop"));
  } else {
    // In both override message and normal running states, show file and progress on the bottom line
    lcd.setCursor(0, 1); lcd.print(filename.substring(0, 10));
    lcd.setCursor(11, 1); lcd.print(progress); lcd.print(F("%"));
  }
}


bool checkCancelButton() {
  static unsigned long lastCancelCheck = 0;
  if (millis() - lastCancelCheck > 50) {
    if (digitalRead(cancelPin) == LOW) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(cancelPin) == LOW) {
        while (digitalRead(cancelPin) == LOW) {}
        Serial.write(0x18); // Send Ctrl+X to reset GRBL
        Serial.flush();
        fileRunning = false;
        filePaused = false;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Job Cancelled"));
        delay(2000);
        showMainMenu();       // Go back to main menu after cancel
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
          Serial.print("!"); // Send Feed Hold
          Serial.flush();
        } else {
          Serial.print("~"); // Send Resume
          Serial.flush();
        }
        return true;
      }
    }
    lastPauseCheck = millis();
  }
  return false;
}

void checkFeedOverrideButtons() {
  // This function is now called from within the file sending loops
  if (!fileRunning || filePaused) return;

  if (digitalRead(upPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(upPin) == LOW) {
      while (digitalRead(upPin) == LOW) {}
      Serial.write(0x91); // GRBL command for Feed Override +10%
      feedOverrideMsg = "Feed +10%";
      showFeedOverrideMessage = true;
      lastFeedOverrideTime = millis();
    }
  }

  if (digitalRead(downPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if (digitalRead(downPin) == LOW) {
      while (digitalRead(downPin) == LOW) {}
      Serial.write(0x92); // GRBL command for Feed Override -10%
      feedOverrideMsg = "Feed -10%";
      showFeedOverrideMessage = true;
      lastFeedOverrideTime = millis();
    }
  }
}

void sendFile(String filename) {
  File dataFile = SD.open(filename);
  if (!dataFile) {
    lcd.clear(); lcd.setCursor(0, 0);
    lcd.print(F("File Error!"));
    delay(2000); return;
  }

  fileRunning = true;
  filePaused = false;
  // ADDED: Set the global filename variable
  currentRunningFile = filename;

  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Running:"));
  lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));

  numLineTotal = 0;
  String line;
  while (dataFile.available()) {
    line = dataFile.readStringUntil('\n');
    line = ignoreUnsupportedCommands(line);
    line.trim();
    if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
      numLineTotal++;
    }
  }

  dataFile.close(); dataFile = SD.open(filename);
  sendCodeLine("G90"); sendCodeLine("G21");
  numLineAct = 0;

  while (dataFile.available() && fileRunning) {
    // ADDED: Check all buttons in the main file sending loop.
    checkFeedOverrideButtons();

    if (checkCancelButton()) {
      dataFile.close();
      return;
    }
    if (checkPauseButton()) {
      int progress = (numLineAct * 100) / numLineTotal;
      updatePauseDisplay(filename, progress);
    }

    if (!filePaused && !awaitingOK) {
      String line = dataFile.readStringUntil('\n');
      line = ignoreUnsupportedCommands(line);
      line.trim();
      if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
        sendCodeLine(line);
        numLineAct++;
        int progress = (numLineAct * 100) / numLineTotal;
        if (!filePaused) {
          updatePauseDisplay(filename, progress);
        }
      }
    } else if (!filePaused) {
      checkForOK();
    }
    delay(1); // MODIFIED: Reduced delay for better button responsiveness
  }

  if (fileRunning) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("Completed"));
    lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));
    delay(3000);
  }

  dataFile.close();
  fileRunning = false;
  filePaused = false;
  // ADDED: Clear the global filename
  currentRunningFile = "";
  showMainMenu();
}

String ignoreUnsupportedCommands(String lineOfCode) {
  removeIfExists(lineOfCode, F("G4"));
  removeIfExists(lineOfCode, F("G10 L2"));
  removeIfExists(lineOfCode, F("G10 l20"));
  removeIfExists(lineOfCode, F("G28"));
  removeIfExists(lineOfCode, F("G30"));
  removeIfExists(lineOfCode, F("G28.1"));
  removeIfExists(lineOfCode, F("G30.1"));
  removeIfExists(lineOfCode, F("G53"));
  removeIfExists(lineOfCode, F("G92"));
  if (lineOfCode.startsWith("/") || lineOfCode.startsWith("T")) lineOfCode = "";
  lineOfCode.trim();
  return lineOfCode;
}

String removeIfExists(String lineOfCode, String toBeRemoved) {
  if (lineOfCode.indexOf(toBeRemoved) >= 0) lineOfCode.replace(toBeRemoved, " ");
  return lineOfCode;
}

void sendCodeLine(String code) {
  if (code.length() == 0) return;
  Serial.println(code);
  Serial.flush();
  awaitingOK = true;
  unsigned long timeout = millis();
  while (awaitingOK && (millis() - timeout < 20000)) {
    checkForOK();
    if (fileRunning) {
      // ADDED: Check for feed override buttons while waiting for GRBL to respond.
      checkFeedOverrideButtons();

      if (checkCancelButton()) return;
      if (checkPauseButton()) {
        int progress = (numLineAct * 100) / numLineTotal;
        // MODIFIED: Use the global filename variable for display consistency
        updatePauseDisplay(currentRunningFile, progress);
      }
    }
    delay(10);
  }
}

void checkForOK() {
  while (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    if (response.indexOf("ok") >= 0) {
      awaitingOK = false;
    }
    if (response.indexOf("error") >= 0) {
      awaitingOK = false;
      if (!fileRunning) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("GRBL Error!"));
        lcd.setCursor(0, 1); lcd.print(response.substring(0, 16));
        delay(2000);
      }
    }
    delay(1);
  }
}

void jog(String axis, float distance, int feed) {
  sendCodeLine("G91");
  sendCodeLine("G1 " + axis + String(distance, 2) + " F" + String(feed));
  sendCodeLine("G90");
}


void showJogMenu() {
  currentMenu = 2;
  selectedOption = 0;
  lcd.clear();
  updateJogMenu();
}

void updateJogMenu() {
  lcd.clear();
  const char* options[] = {"X+", "X-", "Y+", "Y-", "Z+", "Z-", "Back"};
  byte startOption = selectedOption;
  if(startOption > 0) startOption--; // Try to show previous option

  for (byte i = 0; i < 2; i++) {
      lcd.setCursor(0, i);
      byte currentOptionIndex = startOption + i;
      if (currentOptionIndex < 7) {
          lcd.print((currentOptionIndex == selectedOption) ? ">" : " ");
          lcd.print(options[currentOptionIndex]);
      }
  }
}


void handleJogMenu() {
  if (digitalRead(upPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if(digitalRead(upPin) == LOW){
      if (selectedOption > 0) selectedOption--;
      updateJogMenu();
      while (digitalRead(upPin) == LOW) {}
    }
  }
  if (digitalRead(downPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if(digitalRead(downPin) == LOW){
      if (selectedOption < 6) selectedOption++;
      updateJogMenu();
      while (digitalRead(downPin) == LOW) {}
    }
  }
  if (digitalRead(selectPin) == LOW) {
    delay(DEBOUNCE_DELAY);
    if(digitalRead(selectPin) == LOW){
      while (digitalRead(selectPin) == LOW) {}
      switch (selectedOption) {
        case 0: jog("X", 10, 500); break;
        case 1: jog("X", -10, 500); break;
        case 2: jog("Y", 10, 500); break;
        case 3: jog("Y", -10, 500); break;
        case 4: jog("Z", 5, 300); break;
        case 5: jog("Z", -5, 300); break;
        case 6:
          showMainMenu();
          return;
      }
      delay(200);
      updateJogMenu();
    }
  }
}

void loop() {
  static unsigned long lastButtonTime = 0;

  // MODIFIED: The 'if (fileRunning)' block was removed from here.
  // All button checks during a run are now handled inside the
  // sendFile() and sendCodeLine() functions, which is the correct place for them.

  if (currentMenu == 2) {
    handleJogMenu();
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
        if (currentMenu == 0 && selectedOption < 6) {
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
            case 0:
              lcd.clear(); lcd.print(F("Unlocking..."));
              sendCodeLine("$X"); delay(1500); showMainMenu(); break;
            case 1:
              lcd.clear(); lcd.print(F("ZeroXYZ..."));
              sendCodeLine("G10 P0 L20 X0 Y0 Z0"); delay(1500); showMainMenu(); break;
            case 2:
              lcd.clear(); lcd.print(F("ZeroX..."));
              sendCodeLine("G10 P0 L20 X0"); delay(1500); showMainMenu(); break;
            case 3:
              lcd.clear(); lcd.print(F("ZeroY..."));
              sendCodeLine("G10 P0 L20 Y0"); delay(1500); showMainMenu(); break;
            case 4:
              lcd.clear(); lcd.print(F("ZeroZ..."));
              sendCodeLine("G10 P0 L20 Z0"); delay(1500); showMainMenu(); break;
            case 5:
              showFileMenu(); break;
            case 6:
              showJogMenu(); break;
          }
        } else if (currentMenu == 1) {
          if (selectedOption == 0) {
            showMainMenu();
          } else {
            String filename = getFileName(selectedOption);
            if (filename != "") {
              // For simplicity, auto-confirming. You could add a confirmation screen here.
              sendFile(filename);
            }
          }
        }
        while (digitalRead(selectPin) == LOW) {}
      }
    }
  }
}
