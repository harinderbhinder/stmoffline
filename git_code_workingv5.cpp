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
#define DEBOUNCE_DELAY 50 

LiquidCrystal_I2C lcd(0x27, 16, 2);

bool awaitingOK = false;
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
  lcd.setCursor(0, 1); lcd.print(F(" ZeroXY"));
}

void updateMainMenu() {
  lcd.clear();
  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(" ZeroXY"));
  } else if (selectedOption == 1) {
    lcd.setCursor(0, 0); lcd.print(F(" Unlock "));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroXY"));
  } else if (selectedOption == 2) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroXY"));
    lcd.setCursor(0, 1); lcd.print(F(">ZeroZ"));
  } else if (selectedOption == 3) {
    lcd.setCursor(0, 0); lcd.print(F(" ZeroZ"));
    lcd.setCursor(0, 1); lcd.print(F(">Run Files"));
  } else if (selectedOption == 4) {
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

  // Case 1: "Back" is selected (selectedOption = 0)
  if (selectedOption == 0) {
    lcd.setCursor(0, 0); lcd.print(F(">Back"));
    String firstFile = getFileName(1);
    if (firstFile != "") {
      lcd.setCursor(0, 1); lcd.print(F(" ")); // Space instead of '>'
      lcd.print(firstFile.substring(0, 14));
      // if (totalFiles > 1) {
      //   lcd.setCursor(15, 1); lcd.print(F("v")); // Show down arrow
      // }
    }
  }
  // Case 2: A file is selected (selectedOption >= 1)
  else {
    String currentFile = getFileName(selectedOption);
    String nextFile = (selectedOption < totalFiles) ? getFileName(selectedOption + 1) : "";

    // Show current file with '>' if it exists
    if (currentFile != "") {
      lcd.setCursor(0, 0); 
      lcd.print(F(">")); // Highlight current file
      lcd.print(currentFile.substring(0, 14));
      // lcd.setCursor(15, 0); lcd.print(F("^")); // Show up arrow
    }

    // Show next file (if available)
    if (nextFile != "") {
      lcd.setCursor(0, 1); 
      lcd.print(F(" ")); // No highlight
      lcd.print(nextFile.substring(0, 14));
      // if (selectedOption + 1 < totalFiles) {
      //   lcd.setCursor(15, 1); lcd.print(F("v")); // Show down arrow
      // }
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
    lcd.clear(); lcd.setCursor(0, 0);
    lcd.print(F("File Error!"));
    delay(2000); return;
  }
  fileRunning = true;
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Running:"));
  lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));
  dataFile.close(); dataFile = SD.open(filename);
  sendCodeLine("G90"); sendCodeLine("G21");
  while (dataFile.available() && fileRunning) {
    if (!awaitingOK) {
      String line = dataFile.readStringUntil('\n');
      line.trim();
      if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
        sendCodeLine(line);
      }
    }
    checkForOK();
  }
  if (fileRunning) {
    lcd.setCursor(0, 1); lcd.print(F("Completed"));
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
  currentMenu = 2;  // Set the menu to Jog menu
  selectedOption = 0;
  lcd.clear();
  updateJogMenu();
}

void updateJogMenu() {
  lcd.clear();
  const char* options[] = {"X+", "X-", "Y+", "Y-", "Z+", "Z-", "Back"};

  for (byte i = 0; i < 3; i++) {
    lcd.setCursor(0, i);
    // Display '>' for the selected option
    lcd.print((selectedOption + i < 7 && selectedOption == i + selectedOption) ? ">" : " ");
    if (selectedOption + i < 7)
      lcd.print(options[selectedOption + i]);  // Print option
  }
}


void jog(String axis, float distance) {
  sendCodeLine("G91");
  String move = "G1 " + axis + String(distance, 2) + " F500";
  sendCodeLine(move);
  sendCodeLine("G90");
}

// This function handles the Jog menu and ensures proper transitions
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
      case 6: // Back option
        currentMenu = 0; // Reset to Main menu
        selectedOption = 4; // Reset to the Jog option position in main menu
        showMainMenu(); // Show Main menu
        updateMainMenu(); // Ensure the display is updated
        return;
    }
    delay(500);
    updateJogMenu();
  }
}




void loop() {
  static unsigned long lastButtonTime = 0;
  #define DEBOUNCE_DELAY 50  // 50ms debounce delay

  // Handle Jog menu separately when it's the active menu
  if (currentMenu == 2) {
    handleJogMenu();
    return;
  }

  // Only handle menu navigation if we are not in the jog menu
  if (millis() - lastButtonTime > 200) {
    // UP Button
    if (digitalRead(upPin) == HIGH) {
      delay(DEBOUNCE_DELAY);              // Debounce delay
      if (digitalRead(upPin) == HIGH) {   // Confirm button is still pressed
        lastButtonTime = millis();
        
        if (currentMenu == 0 && selectedOption > 0) {
          selectedOption--;
          updateMainMenu();
        } 
        else if (currentMenu == 1 && selectedOption > 0) {
          selectedOption--;
          updateFileMenu();
        }
        
        while (digitalRead(upPin) == HIGH) {} // Wait for button release
      }
    }

    // DOWN Button
    if (digitalRead(downPin) == HIGH) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(downPin) == HIGH) {
        lastButtonTime = millis();
        
        if (currentMenu == 0 && selectedOption < 4) {
          selectedOption++;
          updateMainMenu();
        } 
        else if (currentMenu == 1) {
          byte maxFiles = getFileCount();
          if (selectedOption < maxFiles) {
            selectedOption++;
            updateFileMenu();
          }
        }
        
        while (digitalRead(downPin) == HIGH) {}
      }
    }

    // SELECT Button
    if (digitalRead(selectPin) == HIGH) {
      delay(DEBOUNCE_DELAY);
      if (digitalRead(selectPin) == HIGH) {
        lastButtonTime = millis();
        
        if (currentMenu == 0) {
          switch (selectedOption) {
            case 0: 
              lcd.clear(); lcd.print(F("Unlocking..."));
              sendCodeLine("$X"); delay(1500); showMainMenu(); break;
            case 1: 
              lcd.clear(); lcd.print(F("ZeroXY..."));
              sendCodeLine("G10 P0 L20 X0 Y0"); delay(1500); showMainMenu(); break;
            case 2: 
              lcd.clear(); lcd.print(F("ZeroZ..."));
              sendCodeLine("G10 P0 L20 Z0"); delay(1500); showMainMenu(); break;
            case 3: 
              showFileMenu(); break;
            case 4: 
              showJogMenu(); break;
          }
        } 
        else if (currentMenu == 1) {
          if (selectedOption == 0) {
            showMainMenu();
          } else {
            String filename = getFileName(selectedOption);
            if (filename != "") {
              // lcd.clear(); lcd.print(F("Run file?"));
              // lcd.setCursor(0, 1); lcd.print(filename.substring(0, 16));
              // unsigned long confirmTime = millis();
              bool confirmed = true;
              // while (millis() - confirmTime < 3000) {
              //   if (digitalRead(selectPin) == HIGH) {
              //     while (digitalRead(selectPin) == HIGH) {}
              //     confirmed = true; break;
              //   }
              // }
              if (confirmed) sendFile(filename);
              else updateFileMenu();
            }
          }
        }
        
        while (digitalRead(selectPin) == HIGH) {}
      }
    }
  }
}
