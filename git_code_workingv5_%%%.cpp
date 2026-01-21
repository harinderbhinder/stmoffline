#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define SD_card_Reader   10
#define upPin            2
#define downPin          4
#define selectPin        3

#define JOG_STEP_MM 10

LiquidCrystal_I2C lcd(0x27, 16, 2);

bool awaitingOK = false;
unsigned int numLineTotal = 0, numLineAct = 0;
byte currentMenu = 0; // 0=main, 1=files, 2=jog
byte selectedOption = 0;
bool fileRunning = false;

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);

  pinMode(upPin, INPUT);
  pinMode(downPin, INPUT);
  pinMode(selectPin, INPUT);

  Serial.begin(115200);

  if (!SD.begin(SD_card_Reader)) {
    lcd.setCursor(0, 0);
    lcd.print(F("Card Error!"));
    while(1);
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(" CNC Offline"));
  lcd.setCursor(0, 1); lcd.print(F(" By Harinder"));
  delay(2000);

  sendCodeLine("$10=0");
  showMainMenu();
}

void showMainMenu() {
  currentMenu = 0;
  selectedOption = 0;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F(">Unlock "));
  lcd.setCursor(0, 1); lcd.print(F(" Zero"));
}

void updateMainMenu() {
  lcd.clear();
  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(" Zero"));
  } else if (selectedOption == 1) {
    lcd.setCursor(0, 0); lcd.print(F(" Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(">Zero"));
  } else if (selectedOption == 2) {
    lcd.setCursor(0, 0); lcd.print(F(" Zero"));
    lcd.setCursor(0, 1); lcd.print(F(">Run Files"));
  } else if (selectedOption == 3) {
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
  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Back"));
    String filename = getFileName(1);
    if (filename != "") {
      lcd.setCursor(0, 1); lcd.print(F(" "));
      lcd.print(filename.substring(0, 14));
      if (totalFiles > 1) {
        lcd.setCursor(15, 1); lcd.print(F("v"));
      }
    }
  } else if (selectedOption == 1) {
    lcd.setCursor(0, 0); lcd.print(F("Back"));
    lcd.setCursor(15, 0); lcd.print(F("^"));
    String filename = getFileName(1);
    if (filename != "") {
      lcd.setCursor(0, 1); lcd.print(F(">"));
      lcd.print(filename.substring(0, 14));
      if (totalFiles > 1) {
        lcd.setCursor(15, 1); lcd.print(F("v"));
      }
    }
  } else {
    String currentFile = getFileName(selectedOption);
    String nextFile = getFileName(selectedOption + 1);
    if (currentFile != "") {
      lcd.setCursor(0, 0); lcd.print(F(" "));
      lcd.print(currentFile.substring(0, 14));
      lcd.setCursor(15, 0); lcd.print(F("^"));
    }
    if (nextFile != "") {
      lcd.setCursor(0, 1); lcd.print(F(">"));
      lcd.print(nextFile.substring(0, 14));
      if (selectedOption < totalFiles) {
        lcd.setCursor(15, 1); lcd.print(F("v"));
      }
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

void sendFile(String filename) {
  File dataFile = SD.open(filename);
  if (!dataFile) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("File Error!"));
    delay(2000);
    return;
  }

  fileRunning = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Running:"));
  lcd.setCursor(0, 1);
  lcd.print(filename.substring(0, 16));

  // Count total lines
  unsigned int numLineTotal = 0;
  String line;
  while (dataFile.available()) {
    line = dataFile.readStringUntil('\n');
    line.trim();
    if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
      numLineTotal++;
    }
  }

  // Reset file position
  dataFile.close();
  dataFile = SD.open(filename);

  // Send G-code setup
  sendCodeLine("G90"); // Absolute coordinates
  sendCodeLine("G21"); // Millimeters

  unsigned int numLineAct = 0;

  // Send file line by line
  while (dataFile.available() && fileRunning) {
    if (!awaitingOK) {
      line = dataFile.readStringUntil('\n');
      line.trim();
      // Skip comments and empty lines
      if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
        sendCodeLine(line);
        numLineAct++; // Increment the line counter
        // Calculate and display progress
        int progress = (numLineAct * 100) / numLineTotal;
        lcd.setCursor(12, 1);
        lcd.print(F("          ")); // Clear previous progress
        lcd.setCursor(12, 1);
        lcd.print(progress);
        lcd.print(F("%:"));
      }
    }

    checkForOK();
  }

  if (fileRunning) {
    lcd.setCursor(0, 1);lcd.print(F("Completed"));
    delay(3000);
  }

  dataFile.close();
  fileRunning = false;
  showMainMenu();
}


void sendCodeLine(String code) {
  Serial.println(code);
  awaitingOK = true;
  unsigned long timeout = millis();
  while (awaitingOK && (millis() - timeout < 10000)) {
    checkForOK(); delay(10);
  }
}

void checkForOK() {
  while (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    if (response.indexOf("ok") >= 0) awaitingOK = false;
    delay(1);
  }
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
  
  for (byte i = 0; i < 3; i++) {
    lcd.setCursor(0, i);
    lcd.print((selectedOption + i < 7 && selectedOption == i + selectedOption) ? ">" : " ");
    if (selectedOption + i < 7)
      lcd.print(options[selectedOption + i]);
  }
}

void jog(String axis, float distance) {
  sendCodeLine("G91");
  String move = "G1 " + axis + String(distance, 2) + " F500";
  sendCodeLine(move);
  sendCodeLine("G90");
}

void handleJogMenu() {
  if (digitalRead(upPin) == HIGH) {
    if (selectedOption > 0) selectedOption--;
    updateJogMenu();
    while (digitalRead(upPin) == HIGH) {}
  }
  if (digitalRead(downPin) == HIGH) {
    if (selectedOption < 6) selectedOption++;
    updateJogMenu();
    while (digitalRead(downPin) == HIGH) {}
  }
  if (digitalRead(selectPin) == HIGH) {
    while (digitalRead(selectPin) == HIGH) {}
    switch (selectedOption) {
      case 0: jog("X", JOG_STEP_MM); break;
      case 1: jog("X", -JOG_STEP_MM); break;
      case 2: jog("Y", JOG_STEP_MM); break;
      case 3: jog("Y", -JOG_STEP_MM); break;
      case 4: jog("Z", JOG_STEP_MM); break;  
      case 5: jog("Z", -JOG_STEP_MM); break; 
      case 6: 
        currentMenu = 0; 
        selectedOption = 0; 
        showMainMenu(); 
        return;
    }
    delay(500);
    updateJogMenu();
  }
}


void loop() {
  static unsigned long lastButtonTime = 0;

  if (currentMenu == 2) {
    handleJogMenu();
    return;
  }

  if (millis() - lastButtonTime > 200) {
    if (digitalRead(upPin) == HIGH) {
      lastButtonTime = millis();
      if (currentMenu == 0 && selectedOption > 0) {
        selectedOption--;
        updateMainMenu();
      } else if (currentMenu == 1 && selectedOption > 0) {
        selectedOption--;
        updateFileMenu();
      }
      while (digitalRead(upPin) == HIGH) {}
    }

    if (digitalRead(downPin) == HIGH) {
      lastButtonTime = millis();
      if (currentMenu == 0 && selectedOption < 3) {
        selectedOption++;
        updateMainMenu();
      } else if (currentMenu == 1) {
        byte maxFiles = getFileCount();
        if (selectedOption < maxFiles) {
          selectedOption++;
          updateFileMenu();
        }
      }
      while (digitalRead(downPin) == HIGH) {}
    }

    if (digitalRead(selectPin) == HIGH) {
      lastButtonTime = millis();
      if (currentMenu == 0) {
        switch (selectedOption) {
          case 0: lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Unlocking..."));
            sendCodeLine("$X"); delay(1500); showMainMenu(); break;
          case 1: lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Setting Zero..."));
            sendCodeLine("G10 P0 L20 X0 Y0 Z0"); delay(1500); showMainMenu(); break;
          case 2: showFileMenu(); break;
          case 3: showJogMenu(); break;
        }
      } else if (currentMenu == 1) {
        if (selectedOption == 0) showMainMenu();
        else {
          String filename = getFileName(selectedOption);
          if (filename != "") {
            lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Run file?"));
            lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));
            unsigned long confirmTime = millis();
            bool confirmed = false;
            while (millis() - confirmTime < 3000) {
              if (digitalRead(selectPin) == HIGH) {
                while (digitalRead(selectPin) == HIGH) {}
                confirmed = true; break;
              }
            }
            if (confirmed) sendFile(filename);
            else updateFileMenu();
          }
        }
      }
      while (digitalRead(selectPin) == HIGH) {}
    }
  }
}
