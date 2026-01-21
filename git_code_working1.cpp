#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

#define SD_card_Reader   10
#define upPin            2
#define downPin          3
#define selectPin        4

// Display
LiquidCrystal_I2C lcd(0x27, 16, 2); // 16x2 LCD

// Globals
bool awaitingOK = false;
unsigned int numLineTotal = 0, numLineAct = 0;
byte currentMenu = 0; // 0=main, 1=files
byte selectedOption = 0;
bool fileRunning = false;

void setup() {
  // Display
  lcd.init();
  lcd.backlight();
  lcd.begin(16, 2);
  
  // Inputs with pullup
  pinMode(upPin, INPUT);
  pinMode(downPin, INPUT);
  pinMode(selectPin, INPUT);
  
  // Serial connection
  Serial.begin(115200);
  
  // Initialize SD card
  if (!SD.begin(SD_card_Reader)) {
    lcd.setCursor(0, 0);
    lcd.print("SD Card Error!");
    while(1); // Stop if no SD card
  }
  
  sendCodeLine("$10=0"); // Enable WPos reporting
  showMainMenu();
}

void showMainMenu() {
  currentMenu = 0;
  selectedOption = 0;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(">Unlock GRBL");
  lcd.setCursor(0, 1);
  lcd.print(" Zero Machine");
}

void updateMainMenu() {
  lcd.clear();
  if (selectedOption == 0) {
    lcd.setCursor(0, 0);
    lcd.print(">Unlock GRBL");
    lcd.setCursor(0, 1);
    lcd.print(" Zero Machine");
  } else if (selectedOption == 1) {
    lcd.setCursor(0, 0);
    lcd.print(" Unlock GRBL");
    lcd.setCursor(0, 1);
    lcd.print(">Zero Machine");
  } else if (selectedOption == 2) {
    lcd.setCursor(0, 0);
    lcd.print(" Zero Machine");
    lcd.setCursor(0, 1);
    lcd.print(">Run from Files");
  }
}

void showFileMenu() {
  currentMenu = 1;
  selectedOption = 0;
  lcd.clear();
  
  // Show "Back" as first option
  lcd.setCursor(0, 0);
  lcd.print(">Back");
  
  String filename = getFileName(1);
  if (filename != "") {
    lcd.setCursor(0, 1);
    lcd.print(" ");
    lcd.print(filename.substring(0, 14)); // 14 chars to leave space for arrow
  } else {
    lcd.setCursor(0, 1);
    lcd.print(" No files found");
  }
}

void updateFileMenu() {
  lcd.clear();
  byte totalFiles = getFileCount();
  
  if (selectedOption == 0) {
    // Show Back and first file
    lcd.setCursor(0, 0);
    lcd.print(">Back");
    
    String filename = getFileName(1);
    if (filename != "") {
      lcd.setCursor(0, 1);
      lcd.print(" ");
      lcd.print(filename.substring(0, 14));
      // Add down arrow if more files
      if (totalFiles > 1) {
        lcd.setCursor(15, 1);
        lcd.print("v");
      }
    }
  } else if (selectedOption == 1) {
    // Show Back and first file (first file selected)
    lcd.setCursor(0, 0);
    lcd.print(" Back");
    lcd.setCursor(15, 0);
    lcd.print("^");
    
    String filename = getFileName(1);
    if (filename != "") {
      lcd.setCursor(0, 1);
      lcd.print(">");
      lcd.print(filename.substring(0, 14));
      // Add down arrow if more files
      if (totalFiles > 1) {
        lcd.setCursor(15, 1);
        lcd.print("v");
      }
    }
  } else {
    // Show current and next file
    String currentFile = getFileName(selectedOption);
    String nextFile = getFileName(selectedOption + 1);
    
    if (currentFile != "") {
      lcd.setCursor(0, 0);
      lcd.print(" ");
      lcd.print(currentFile.substring(0, 14));
      lcd.setCursor(15, 0);
      lcd.print("^");
    }
    
    if (nextFile != "") {
      lcd.setCursor(0, 1);
      lcd.print(">");
      lcd.print(nextFile.substring(0, 14));
      // Add down arrow if more files
      if (selectedOption < totalFiles) {
        lcd.setCursor(15, 1);
        lcd.print("v");
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

void sendFile(String filename) {
  File dataFile = SD.open(filename);
  if (!dataFile) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("File Error!");
    delay(2000);
    return;
  }
  
  fileRunning = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Running:");
  lcd.setCursor(0, 1);
  lcd.print(filename.substring(0, 16));
  
  // Count total lines
  numLineTotal = 0;
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
  
  numLineAct = 0;
  
  // Send file line by line
  while (dataFile.available() && fileRunning) {
    if (!awaitingOK) {
      line = dataFile.readStringUntil('\n');
      line.trim();
      
      // Skip comments and empty lines
      if (line.length() > 0 && !line.startsWith(";") && !line.startsWith("(")) {
        sendCodeLine(line);
        numLineAct++;
        
        // Update progress
        int progress = (numLineAct * 100) / numLineTotal;
        lcd.setCursor(12, 1);
        if (progress < 10) lcd.print(" ");
        if (progress < 100) lcd.print(" ");
        lcd.print(progress);
        lcd.print("%");
      }
    }
    
    checkForOK();
    
    // Check for abort
    if (digitalRead(selectPin) == HIGH) {
      delay(50);
      while (digitalRead(selectPin) == HIGH) {}
      Serial.println("!"); // Feed hold
      delay(100);
      Serial.write(24); // Soft reset
      fileRunning = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Aborted!");
      delay(2000);
      break;
    }
  }
  
  if (fileRunning) {
    lcd.setCursor(0, 1);
    lcd.print("Complete!   100%");
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
    checkForOK();
    delay(10);
  }
}

void checkForOK() {
  while (Serial.available()) {
    String response = Serial.readStringUntil('\n');
    if (response.indexOf("ok") >= 0) {
      awaitingOK = false;
    }
    delay(1);
  }
}

void loop() {
  static unsigned long lastButtonTime = 0;
  
  if (millis() - lastButtonTime > 200) { // Debounce
    // Up button
    if (digitalRead(upPin) == HIGH) {
      lastButtonTime = millis();
      if (currentMenu == 0) { // Main menu
        if (selectedOption > 0) {
          selectedOption--;
          updateMainMenu();
        }
      } else if (currentMenu == 1) { // File menu
        if (selectedOption > 0) {
          selectedOption--;
          updateFileMenu();
        }
      }
      while (digitalRead(upPin) == HIGH) {}
    }
    
    // Down button
    if (digitalRead(downPin) == HIGH) {
      lastButtonTime = millis();
      if (currentMenu == 0) { // Main menu
        if (selectedOption < 2) {
          selectedOption++;
          updateMainMenu();
        }
      } else if (currentMenu == 1) { // File menu
        byte maxFiles = getFileCount();
        if (selectedOption < maxFiles) { // +1 for Back option
          selectedOption++;
          updateFileMenu();
        }
      }
      while (digitalRead(downPin) == HIGH) {}
    }
    
    // Select button
    if (digitalRead(selectPin) == HIGH) {
      lastButtonTime = millis();
      
      if (currentMenu == 0) { // Main menu
        switch (selectedOption) {
          case 0: // Unlock GRBL
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Unlocking...");
            sendCodeLine("$X");
            lcd.setCursor(0, 1);
            lcd.print("Done!");
            delay(1500);
            showMainMenu();
            break;
            
          case 1: // Zero Machine
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Setting Zero...");
            sendCodeLine("G10 P0 L20 X0 Y0 Z0");
            lcd.setCursor(0, 1);
            lcd.print("Done!");
            delay(1500);
            showMainMenu();
            break;
            
          case 2: // Run from Files
            showFileMenu();
            break;
        }
              } else if (currentMenu == 1) { // File menu
        if (selectedOption == 0) {
          // Back option selected
          showMainMenu();
        } else {
          String filename = getFileName(selectedOption);
          if (filename != "") {
            // Confirm file selection
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Run file?");
            lcd.setCursor(0, 1);
            lcd.print(filename.substring(0, 16));
            
            unsigned long confirmTime = millis();
            bool confirmed = false;
            
            while (millis() - confirmTime < 3000) {
              if (digitalRead(selectPin) == HIGH) {
                while (digitalRead(selectPin) == HIGH) {}
                confirmed = true;
                break;
              }
            }
            
            if (confirmed) {
              sendFile(filename);
            } else {
              updateFileMenu();
            }
          }
        }
      }
      
      while (digitalRead(selectPin) == HIGH) {}
    }
  }
}