#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define upPin       PA0
#define downPin     PA1
#define selectPin   PA2
#define cancelPin   PA3
#define SD_card_Reader PA4

#define DEBOUNCE_DELAY 50

LiquidCrystal_I2C lcd(0x27, 16, 2);

bool awaitingOK = false;
unsigned int numLineTotal = 0, numLineAct = 0;
byte currentMenu = 0;
byte selectedOption = 0;
bool fileRunning = false;
bool filePaused = false;

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

void updatePauseDisplay(String filename, int progress) {
  lcd.clear();
  if (filePaused) {
    lcd.setCursor(0, 0); lcd.print(F("PAUSED"));
    lcd.setCursor(7, 0); lcd.print(progress); lcd.print(F("%"));
    lcd.setCursor(0, 1); lcd.print(F("SEL:Resume CAN:Stop"));
  } else {
    lcd.setCursor(0, 0); lcd.print(F("Running:"));
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
        Serial.write(0x18);
        Serial.flush();
        fileRunning = false;
        filePaused = false;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Job Cancelled"));
        delay(2000);
        showMainMenu();        // Go back to main menu after cancel
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
          Serial.print("!");
          Serial.flush();
        } else {
          Serial.print("~");
          Serial.flush();
        }
        return true;
      }
    }
    lastPauseCheck = millis();
  }
  return false;
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
    delay(10);
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
      if (checkCancelButton()) return;
      if (checkPauseButton()) {
        int progress = (numLineAct * 100) / numLineTotal;
        updatePauseDisplay("", progress);
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

// // NEW WAIT FOR IDLE
// bool waitForIdle(unsigned long timeoutMillis) {
//   unsigned long startTime = millis();
//   lcd.clear();
//   lcd.setCursor(0, 0);
//   lcd.print("Moving...");

//   while (millis() - startTime < timeoutMillis) {
//     Serial.println("?");
//     Serial.flush();
//     delay(150);
//     while (Serial.available()) {
//       String response = Serial.readStringUntil('\n');
//       response.trim();
//       if (response.startsWith("<Idle")) {
//         lcd.clear();
//         lcd.setCursor(0, 0);
//         lcd.print("Move Done");
//         delay(500);
//         return true;
//       }
//     }
//     if (checkCancelButton()) {
//       return false;
//     }
//   }

//   lcd.clear();
//   lcd.print("Timeout!");
//   delay(1000);
//   return false;
// }

// UPDATED JOG
void jog(String axis, float distance, int feed) {
  sendCodeLine("G91");
  sendCodeLine("G1 " + axis + String(distance, 2) + " F" + String(feed));
  sendCodeLine("G90");
  // waitForIdle(30000);
  // updateJogMenu();
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
  for (byte i = 0; i < 2; i++) {
    lcd.setCursor(0, i);
    lcd.print((selectedOption + i < 7 && selectedOption == i + selectedOption) ? ">" : " ");
    if (selectedOption + i < 7)
      lcd.print(options[selectedOption + i]);
  }
}

void handleJogMenu() {
  if (digitalRead(upPin) == LOW) {
    if (selectedOption > 0) selectedOption--;
    updateJogMenu();
    while (digitalRead(upPin) == LOW) {}
  }
  if (digitalRead(downPin) == LOW) {
    if (selectedOption < 6) selectedOption++;
    updateJogMenu();
    while (digitalRead(downPin) == LOW) {}
  }
  if (digitalRead(selectPin) == LOW) {
    while (digitalRead(selectPin) == LOW) {}
    switch (selectedOption) {
      case 0: jog("X", 10, 500); break;
      case 1: jog("X", -10, 500); break;
      case 2: jog("Y", 10, 500); break;
      case 3: jog("Y", -10, 500); break;
      case 4: jog("Z", 5, 300); break;
      case 5: jog("Z", -5, 300); break;
      case 6:
        currentMenu = 0;
        selectedOption = 6;
        showMainMenu();
        updateMainMenu();
        return;
    }
    delay(500);
    updateJogMenu();
  }
}

void loop() {
  static unsigned long lastButtonTime = 0;

  if (fileRunning) {
    return;
  }

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
              bool confirmed = true;
              if (confirmed) sendFile(filename);
              else updateFileMenu();
            }
          }
        }
        while (digitalRead(selectPin) == LOW) {}
      }
    }
  }
}
