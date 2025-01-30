/*
  ************************************************************************************
  * MIT License
  *
  * Copyright (c) 2023 Crunchlabs LLC (Laser Tag Code)

  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is furnished
  * to do so, subject to the following conditions:
  *
  * The above copyright notice and this permission notice shall be included in all
  * copies or substantial portions of the Software.
  *
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
  * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  *
  ************************************************************************************
*/

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> FEATURES <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

#define RF_COMMS

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> PIN DEFINITIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define IR_SEND_PIN         3
#define IR_RECEIVE_PIN      5
#define _IR_TIMING_TEST_PIN 7

#define SERVO_PIN       9

#if defined(RF_COMMS)
  #define RELOAD_PIN      14 // A0 pin
  #define BUZZER_PIN      16 // A2 pin
  #define TRIGGER_PIN     17 // A3 pin

  #define COMMS_CE        2
  #define COMMS_CSN       4
#else
  #define RELOAD_PIN      10
  #define BUZZER_PIN      11
  #define TRIGGER_PIN     12
#endif

// #define TEAM1_PIN       15      // A1 pin
// #define TEAM2_PIN       16      // A2 pin
// #define TEAM3_PIN       17      // A3 pin

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> LIBRARIES <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define DECODE_NEC              // defines RIR Protocol (Apple and Onkyo)

#include <IRremote.hpp>
#include <Arduino.h>
#include <Servo.h>
#include <U8g2lib.h>

#if defined(RF_COMMS)
  #include <SPI.h>
  #include <nRF24L01.h>
  #include <RF24.h>

  RF24 radio(COMMS_CE, COMMS_CSN); // CE, CSN
#endif

Servo myservo;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

int numHalfWidth[20] = {9,7,9,9,8,9,9,9,9,9,19,16,19,19,19,19,19,19,19,19};

// >>>>>>>>>>>>>>>>>>>>>>>>>>> GAME PARAMETERS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define DEBOUNCE_DELAY 20

#define SERVO_INITIAL_POS 150     // how agressively to undarken goggles
#define SERVO_READY_POS 120       // reduce aggresiveness near end of action
#define SERVO_STOP 90             // Stop the servo
#define SERVO_HIT_POS 50          // Darken all the way
#define SERVO_TIME_TO_33 400      // ms needed to darken by 1/3
#define SERVO_TIME_TO_66 800      // ms needed to darken by 2/3
#define SERVO_TIME_TO_100 1200    // ms needed to darken all the way

#define TRIGGER_COOLDOWN 500      // milliseconds
#define HIT_TIMEOUT 8000          // milliseconds
#define RELOAD_TIME_EACH 1000     // milliseconds
#define CHARGE_ULTRA_BEAM 5000    // milliseconds

const bool infiniteAmmo = false;
const int maxAmmo = 6;
const bool deathTakesAmmo = true;

int team = 1;     // default

// >>>>>>>>>>>>>>>>>>>>>>>>>>> GAME VARIABLES <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

int lastTriggerVal = 1;                     // trigger debounce variable
unsigned long lastTDebounceTime = 0;        // trigger button debounce time
int triggerState;                           // trigger debounce result
bool buttonWasReleased = true;              // release check, no "full auto"
unsigned long previousTriggerMillis = 0;    // cooldown timestamp between shots

int lastReloadVal = 1;                      // reload button, debounce
unsigned long lastRDebounceTime = 0;        // reload button debounce time
int reloadState;                            // reload button debounce result

bool isReloading = false;                   // allows reloading sequence
unsigned long reloadTimer = -CHARGE_ULTRA_BEAM - 1000; // time to add shots to ammo bar
int ammo = maxAmmo;                         // current ammo bootup at max

int health = 3; // of 3 maximum
int lives = 2;

int hitTeam = 0;
int hitUltraBeam = 0;
int ultraBeamRemaining = 1;
bool ultraBeamCharged = false;
bool gameOver = false;

// Initialize game timeout variable
unsigned long timeoutStartTime = -HIT_TIMEOUT - 1000;

// IR pulse, tracks team distinction
uint8_t sCommand;                            // IR command being sent
uint8_t sUltraBeam;                          // IR command being sent
uint8_t rcvCommand1;                         // IR command being recieved
uint8_t rcvUltraBeam1;                       // IR command being recieved
uint8_t rcvCommand2;                         // IR command being recieved
uint8_t rcvUltraBeam2;                       // IR command being recieved

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> DISPLAY VARIABLES <<<<<<<<<<<<<<<<<<<<<<<<<<
bool pageDone = false;

// Values cannot change while still drawing screen
int drawAmmo = ammo;
int drawHealth = health;
int drawLives = lives;
int drawReloadArc = 0;
int drawHitTeam = 0;
int drawHitUltraBeam = 0;
int drawUltraBeamRemaining = ultraBeamRemaining;
float drawCountdownBox = 0;
bool drawReloading = false;
bool drawUltraBeamCharged = false;
bool drawGameOver = false;
bool drawChargingUltraBeam = false;

static const unsigned char heart_bitmap[] U8X8_PROGMEM = {
  0x00, 0x00,         // 00000000 00000000
  0xC6, 0x00,         // 11000110 00000000
  0xEF, 0x01,         // 11101111 00000001
  0xFF, 0x01,         // 11111111 00000001
  0xFE, 0x00,         // 11111110 00000000
  0x7C, 0x00,         // 01111100 00000000
  0x38, 0x00,         // 00111000 00000000
  0x10, 0x00          // 00010000 00000000
};

static const unsigned char ub_bitmap[] U8X8_PROGMEM = {
  0xE0,               // 11100000
  0xB0,               // 10110000
  0xD8,               // 11011000
  0x6C,               // 01101100
  0x37,               // 00110111
  0x1B,               // 00011011
  0x0E,               // 00001110
  0x0C                // 00001100
};

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> SETUP <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void setup() {
  Serial.begin(115200);

  u8g2.begin();

  // Move Goggles to start config
  myservo.attach(SERVO_PIN);
  myservo.write(SERVO_INITIAL_POS);
  delay(500);
  myservo.write(SERVO_READY_POS);
  delay(500);
  myservo.detach();

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(RELOAD_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  // pinMode(TEAM1_PIN, INPUT_PULLUP);
  // pinMode(TEAM2_PIN, INPUT_PULLUP);
  // pinMode(TEAM3_PIN, INPUT_PULLUP);

  // if (digitalRead(TEAM1_PIN) == LOW) {
  team = 1;
  // } else if (digitalRead(TEAM2_PIN) == LOW) {
  //   team = 2;
  // } else if (digitalRead(TEAM3_PIN) == LOW) {
  //   team = 3;
  // }

  if (team == 1) {
    sCommand = 0x34;
    sUltraBeam = 0x37;
    rcvCommand1 = 0x35;
    rcvUltraBeam1 = 0x38;
    rcvCommand2 = 0x36;
    rcvUltraBeam2 = 0x39;
  } else if (team == 2) {
    sCommand = 0x35;
    sUltraBeam = 0x38;
    rcvCommand1 = 0x34;
    rcvUltraBeam1 = 0x37;
    rcvCommand2 = 0x36;
    rcvUltraBeam2 = 0x39;
  } else {
    sCommand = 0x36;
    sUltraBeam = 0x39;
    rcvCommand1 = 0x34;
    rcvUltraBeam1 = 0x37;
    rcvCommand2 = 0x35;
    rcvUltraBeam2 = 0x38;
  }

  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  // Set draw settings
  u8g2.setBitmapMode(1);
  u8g2.setDrawColor(2);
  u8g2.setFontMode(1);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> LOOP <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void loop() {
  unsigned long currentMillis = millis();

  handleTrigger(currentMillis);
  handleIRReception();
  ReadReloadButton(currentMillis);

  if (isReloading && (ammo < maxAmmo || (ultraBeamRemaining > 0 && !ultraBeamCharged))) {
    UpdateAmmo(true, currentMillis);
  }

  updateDisplay(currentMillis);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> UI FUNCTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// Draw screen only as needed to reduce processing time
void updateDisplay(unsigned long currentMillis) {
  if (pageDone) {
    // If page is done drawing, check to see if it needs updating
    if (ammo != drawAmmo || health != drawHealth || hitTeam != drawHitTeam || drawGameOver != gameOver
        || ultraBeamCharged != drawUltraBeamCharged || drawUltraBeamRemaining != ultraBeamRemaining
        || drawHitUltraBeam != hitUltraBeam || isReloading || drawReloading
        || currentMillis - timeoutStartTime < HIT_TIMEOUT) {
      drawChargingUltraBeam = false;
      if (ammo == maxAmmo && isReloading && ultraBeamRemaining > 0) {
        drawReloadArc = max(255 - (currentMillis - reloadTimer) * 255 / CHARGE_ULTRA_BEAM, 0);
        drawChargingUltraBeam = true;
      } else {
        drawReloadArc = max(255 - (currentMillis - reloadTimer) * 255 / RELOAD_TIME_EACH, 0);
      }
      drawCountdownBox = min((float)(currentMillis - timeoutStartTime) / HIT_TIMEOUT, 1.0);
      drawAmmo = ammo;
      drawHealth = health;
      drawHitTeam = hitTeam;
      drawGameOver = gameOver;
      drawReloading = isReloading;
      drawHitUltraBeam = hitUltraBeam;
      drawUltraBeamCharged = ultraBeamCharged;
      drawUltraBeamRemaining = ultraBeamRemaining;
      pageDone = false;
    }
    if (!pageDone) {
      u8g2.firstPage();
    }
  }
  if (!pageDone) {
    // Team
    u8g2.setCursor(118, 0);
    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.setFontPosTop();
    u8g2.print("T");
    u8g2.print(team);

    for (int x = 0; x < lives; x++) {
      u8g2.drawXBMP(0 + x * 11, 0, 16, 8, heart_bitmap);
    }

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setFontPosTop();
    
    if (drawGameOver) {
      u8g2.setCursor(35, 9);
      u8g2.print("GAME");
      u8g2.setCursor(35, 27);
      u8g2.print("OVER");
    } else if (drawHitTeam > 0 || drawHitUltraBeam > 0) {
      // Cooldown bar
      u8g2.drawBox(0, 0, 128 * (1.0 - drawCountdownBox), 47);
      
      if (drawHitTeam == team) {
        u8g2.setCursor(10, 9);
        u8g2.print("OVERLOAD");
        u8g2.setCursor(20, 27);
        u8g2.print("DAMAGE");
      } else if (drawHitUltraBeam > 0) {
        u8g2.setCursor(0, 9);
        u8g2.print("ULTRA BEAM");
        u8g2.setCursor(0, 27);
        u8g2.print("FROM TEAM ");
        u8g2.print(drawHitUltraBeam);
      } else {
        u8g2.setCursor(20, 9);
        u8g2.print("HIT FROM");
        u8g2.setCursor(20, 27);
        u8g2.print("TEAM ");
        u8g2.print(drawHitTeam);
      }
    } else if (drawUltraBeamCharged) {
      u8g2.setCursor(1, 9);
      u8g2.print("ULTRA BEAM");
      u8g2.setCursor(17, 27);
      u8g2.print("CHARGED");
    } else {
      // Ammo
      u8g2.setCursor(64 - numHalfWidth[drawAmmo], 31);
      u8g2.setFont(u8g2_font_helvB24_tn);
      u8g2.setFontPosCenter();
      u8g2.print(drawAmmo);

      // Reload Arc
      if (drawReloading && ammo < maxAmmo || drawChargingUltraBeam) {
        u8g2.drawArc(64, 27, 20, drawReloadArc, 0);
        if (drawChargingUltraBeam) {
          u8g2.drawArc(64, 27, 18, drawReloadArc, 0);
          u8g2.drawArc(64, 27, 16, drawReloadArc, 0);
        }
      }

      for (int x = 0; x < drawUltraBeamRemaining; x++) {
        u8g2.drawXBMP(0 + x * 11, 40, 8, 8, ub_bitmap);
      }
    }

    // Health
    if (drawHealth > 0) {
      u8g2.drawTriangle(0,55, 40,53, 0,63);
      u8g2.drawTriangle(40,63, 40,53, 0,63);
    }
    if (drawHealth > 1) {
      u8g2.drawTriangle(43,53, 84,50, 43,63);
      u8g2.drawTriangle(84,63, 84,50, 43,63);
    }
    if (drawHealth > 2) {
      u8g2.drawTriangle(87,50, 127,48, 87,63);
      u8g2.drawTriangle(127,63, 127,48, 87,63);
    }
    if (!u8g2.nextPage()) {
      pageDone = true;
    }
  }
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> GAMEPLAY FUNCTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// Read Trigger ----------
void handleTrigger(unsigned long currentMillis) {
  if (ReadTriggerButton() && buttonWasReleased && ammo > 0 && currentMillis - previousTriggerMillis >= TRIGGER_COOLDOWN) {
    previousTriggerMillis = currentMillis;
    buttonWasReleased = false;
    isReloading = false;

    digitalWrite(BUZZER_PIN, HIGH);
    sendIR_Pulse();
    UpdateAmmo(false, currentMillis);
  } else if (!ReadTriggerButton()) {
    buttonWasReleased = true;
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// Fire "Shot" ----------
void sendIR_Pulse() {
  Serial.flush();
  if (ultraBeamCharged) {
    IrSender.sendNEC(0x00, sUltraBeam, 5);
    markHit(true);
  } else {
    IrSender.sendNEC(0x00, sCommand, 3);
  }
  delay(10);
}

// Read incoming message ----------
void handleIRReception() {
  if (IrReceiver.decode()) {
    checkPlayerHit();
    IrReceiver.resume(); // Ensure IR receiver is reset
  }
}

// Check if message is a "shot" from an enemy team ----------
void checkPlayerHit() {
  if (IrReceiver.decodedIRData.command == rcvCommand1 || IrReceiver.decodedIRData.command == rcvCommand2
      || IrReceiver.decodedIRData.command == rcvUltraBeam1 || IrReceiver.decodedIRData.command == rcvUltraBeam2) {
    if (millis() - timeoutStartTime > HIT_TIMEOUT + 1000) {
      markHit(false);
    }
  }
}

// Move goggles if hit ----------
void markHit(bool self) {
  // get current time
  timeoutStartTime = millis();

  if (self) {
    hitTeam = team;
  } else if (IrReceiver.decodedIRData.command == 0x34) {
    hitTeam = 1;
  } else if (IrReceiver.decodedIRData.command == 0x35) {
    hitTeam = 2;
  } else if (IrReceiver.decodedIRData.command == 0x36) {
    hitTeam = 3;
  } else if (IrReceiver.decodedIRData.command == 0x37) {
    hitUltraBeam = 1;
  } else if (IrReceiver.decodedIRData.command == 0x38) {
    hitUltraBeam = 2;
  } else if (IrReceiver.decodedIRData.command == 0x39) {
    hitUltraBeam = 3;
  }

  // move goggles to darken
  myservo.attach(SERVO_PIN);

  int prevHealth = health;
  if (hitUltraBeam > 0) {
    health = 0;
  } else {
    health -= 1;
  }

  myservo.write(SERVO_HIT_POS);
  unsigned long servoStartTime = millis();
  int runTime = 0;
  if (prevHealth - health == 1) {
    runTime = SERVO_TIME_TO_33;
  } else if (prevHealth - health == 2) {
    runTime = SERVO_TIME_TO_66;
  } else if (prevHealth - health >= 3) {
    runTime = SERVO_TIME_TO_100;
  }

  if (health <= 0) {
    runTime + 300;
    lives -= 1;
  }

  if (lives == 0) {
    gameOver = true;
  }
  
  digitalWrite(BUZZER_PIN, HIGH);

  // Vibrate buzzer during timeout. !!! section is blocking !!!
  while (millis() - timeoutStartTime < HIT_TIMEOUT) {
    if (millis() - servoStartTime > runTime) {
      myservo.write(SERVO_STOP);
    }

    // In last 20% of timeout, begin to move servo towards starting position
    int timeVal = (millis() - timeoutStartTime) / 100;
    if (health == 0 && millis() > timeoutStartTime + (HIT_TIMEOUT * (4.0 / 5.0))) {
      myservo.write(SERVO_INITIAL_POS);
    }

    updateDisplay(millis());

    // Pulse the buzzer (uses moduluo millis)
    if (timeVal % 10 < 1) {
      digitalWrite(BUZZER_PIN, LOW);
    } else if (millis() < timeoutStartTime + min(HIT_TIMEOUT * (4.0 / 5.0), 3000)) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // After time out reset buzzer
  digitalWrite(BUZZER_PIN, LOW);
  hitTeam = 0;
  hitUltraBeam = 0;

  isReloading = false;
  if (health <= 0 && !gameOver) {
    if (deathTakesAmmo && !infiniteAmmo) {
      ammo = 0;
    }
    health = 3;
  }

  myservo.detach();
}

// >>>>>>>>>>>>>>>>>>>>> RUN UI COMPONENTS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void UpdateAmmo(bool dir, unsigned long currentMillis) {
  if (ammo > maxAmmo) {
    ammo = maxAmmo;
    if (ultraBeamRemaining > 0) {
      isReloading = false;
    }
  }
  if (!infiniteAmmo) {
    if (!dir) {
      if (ultraBeamCharged) {
        ammo = 0;
        ultraBeamCharged = false;
      } else {
        ammo -= 1;
      }
    } else {
      if (currentMillis - reloadTimer > RELOAD_TIME_EACH && ammo < maxAmmo) {
        ammo += 1;
        reloadTimer = currentMillis;
      }
      if (currentMillis - reloadTimer > CHARGE_ULTRA_BEAM && ultraBeamRemaining > 0) {
        ultraBeamCharged = true;
        ultraBeamRemaining -= 1;                             
      }
    }
  }
}

// >>>>>>>>>>>>>>>>>>>>>>> BUTTON DEBOUNCE FILTERS <<<<<<<<<<<<<<<<<<<<<<<<<
bool ReadTriggerButton() {
  int triggerVal = digitalRead(TRIGGER_PIN);
  if (triggerVal != lastTriggerVal) {
    lastTDebounceTime = millis();
  }
  if ((millis() - lastTDebounceTime) > DEBOUNCE_DELAY) {
    if (triggerVal != triggerState) {
      triggerState = triggerVal;
    }
  }
  lastTriggerVal = triggerVal;
  return triggerState == LOW;
}

void ReadReloadButton(unsigned long currentMillis) {
  int reloadVal = digitalRead(RELOAD_PIN);
  if (reloadVal != lastReloadVal) {
    lastRDebounceTime = currentMillis;
  }
  if ((currentMillis - lastRDebounceTime) > DEBOUNCE_DELAY) {
    if (reloadVal != reloadState) {
      reloadState = reloadVal;
      if (reloadState == LOW) {
        reloadTimer = currentMillis;
      }
    }
  }
  lastReloadVal = reloadVal;
  if (reloadState == LOW) {
    isReloading = true;
  } else {
    isReloading = false;
  }
}
