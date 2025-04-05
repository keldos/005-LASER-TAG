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

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> PIN DEFINITIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define IR_SEND_PIN         3
#define IR_RECEIVE_PIN      5
#define _IR_TIMING_TEST_PIN 7

#define SERVO_PIN       9

#define RELOAD_PIN      14 // A0 pin
#define BUZZER_PIN      16 // A2 pin
#define TRIGGER_PIN     17 // A3 pin

#define COMMS_CE        2
#define COMMS_CSN       4

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> LIBRARIES <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define DECODE_NEC              // defines RIR Protocol (Apple and Onkyo)

#include <IRremote.hpp>
#include <Arduino.h>
#include <Servo.h>
#include <U8g2lib.h>

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(COMMS_CE, COMMS_CSN); // CE, CSN
const byte address[6] = "00001";

enum class PacketType {
  Undefined,
  IAm,
  IAmError,
  StartGame,
  EMP,
  ImHit,
  ImEliminated,
};

struct DataPacket {
  unsigned int uniqueID = 0;
  PacketType type = PacketType::Undefined;
  uint8_t playerId = 255;
};

#define MAX_TEAM_PLAYERS    8 // Must be even!
#define ID_OFFSET           MAX_TEAM_PLAYERS
#define ID_RANGE            MAX_TEAM_PLAYERS * 2
unsigned long lastHeartbeat = 0;
unsigned int randomDelay = 1245;

char errorMsg[50];
bool displayError = false;

Servo myservo;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

uint8_t numHalfWidth[10] = {9,7,9,9,8,9,9,9,9,9};

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
#define START_GAME_HOLD 2000      // milliseconds
#define EMP_HOLD 1000             // milliseconds
#define START_GAME_COUNTDOWN 4000 // milliseconds
#define DISABLED_TIMEOUT 4000     // milliseconds
#define SHIELD_TIMER 20000        // milliseconds
#define RELOAD_TIME_EACH 1000     // milliseconds
#define CHARGE_ULTRA_BEAM 5000    // milliseconds
#define HIT_INDICATOR 2000        // milliseconds

const uint8_t maxAmmo = 6;
const uint8_t maxEMP = 1;
const bool deathTakesAmmo = true;
uint8_t playerId = 255;
uint8_t playerIdx = 255;
unsigned int uniqueID;
bool eliminated = false;
uint8_t eliminatedMe = 0;

uint8_t lastTriggerVal = 1;                 // trigger debounce variable
unsigned long lastTDebounceTime = 0;        // trigger button debounce time
uint8_t triggerState;                       // trigger debounce result
bool buttonWasReleased = true;              // release check, no "full auto"
unsigned long previousTriggerMillis = 0;    // cooldown timestamp between shots
unsigned long hitIndicatorTime = 0;         // hit indicator timeout

uint8_t lastReloadVal = 1;                  // reload button, debounce
unsigned long lastRDebounceTime = 0;        // reload button debounce time
uint8_t reloadState;                        // reload button debounce result
unsigned long buzzerTime = 0;               // when the buzzer should stop

unsigned long reloadTimer = -CHARGE_ULTRA_BEAM - 1000; // time to add shots to ammo bar

enum class GamePhase {
  PressStart,
  TeamSelect,
  GameCountdown,
  GameInProgress,
  GameOver,
};

enum class HitIndicator {
  None,
  Hit,
  UltraBeamHit,
  Elimination,
};

struct GameData {
  uint8_t ammo = maxAmmo;
  uint8_t emp = 0;
  uint8_t team = 3;
  uint8_t health = 3;
  uint8_t lives = 2;
  uint8_t hitTeam = 0;
  uint8_t hitUltraBeam = 0;
  uint8_t ultraBeamRemaining = 1;
  uint8_t team1PlayerCount = 0;
  uint8_t team2PlayerCount = 0;
  uint8_t team3PlayerCount = 0;
  bool shielded = false;
  bool disabled = false;
  bool isReloading = false;
  bool ultraBeamCharged = false;
  bool victory = false;
  HitIndicator hitIndicator = HitIndicator::None;
  GamePhase phase = GamePhase::PressStart;
};

GameData currentGD;

// These values should not change while still drawing screen
GameData drawGD;

struct PlayerData {
  unsigned int uniqueID = 0;
  unsigned int lastHeartbeat = 0;
  bool eliminated = false;
};

// Keep track of player stats
PlayerData registeredPlayers[3][MAX_TEAM_PLAYERS];

// Initialize game timeout variable
unsigned long timeoutStartTime = -HIT_TIMEOUT - 1000;
bool hitTimeout = false;
unsigned long gameInitiatedTime = 0;
unsigned long shieldStartTime = 0;
unsigned long disabledStartTime = 0;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> DISPLAY VARIABLES <<<<<<<<<<<<<<<<<<<<<<<<<<
bool pageDone = false;
float countdownBox = 0;
bool chargingUltraBeam = false;
bool chargingEMP = false;
uint8_t reloadArc = 0;
float empRadius = 0;
uint8_t gameStartCountdown = 3;

static const unsigned char heart_bitmap[] U8X8_PROGMEM = {
  0x00, 0x00,  // 00000000 00000000
  0xC6, 0x00,  // 11000110 00000000
  0xEF, 0x01,  // 11101111 00000001
  0xFF, 0x01,  // 11111111 00000001
  0xFE, 0x00,  // 11111110 00000000
  0x7C, 0x00,  // 01111100 00000000
  0x38, 0x00,  // 00111000 00000000
  0x10, 0x00   // 00010000 00000000
};

static const unsigned char ub_bitmap[] U8X8_PROGMEM = {
  0xE0,        // 11100000
  0xB0,        // 10110000
  0xD8,        // 11011000
  0x6C,        // 01101100
  0x37,        // 00110111
  0x1B,        // 00011011
  0x0E,        // 00001110
  0x0C         // 00001100
};

static const unsigned char remote_player_bitmap[] U8X8_PROGMEM = {
  0x00,        // 00000000
  0x0C,        // 00001100
  0x12,        // 00010010
  0x12,        // 00010010
  0x0C,        // 00001100
  0x12,        // 00010010
  0x21,        // 00100001
  0x3F,        // 00111111
};

static const unsigned char player_bitmap[] U8X8_PROGMEM = {
  0x00,        // 00000000
  0x0C,        // 00001100
  0x1E,        // 00011110
  0x1E,        // 00011110
  0x0C,        // 00001100
  0x1E,        // 00011110
  0x3F,        // 00111111
  0x3F,        // 00111111
};

static const unsigned char emp_bitmap[] U8X8_PROGMEM = {
  0x18,        // 00011000
  0x3C,        // 00111100
  0x66,        // 01100110
  0xE7,        // 11100111
  0x99,        // 10011001
  0x5A,        // 01011010
  0x3C,        // 00111100
  0x7E         // 01111110
};

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> SETUP <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void setup() {
  // Serial.begin(9600);

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

  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  // Set up RF module
  radio.begin();
  radio.setChannel(76);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address); // Add reading pipe
  radio.setPALevel(RF24_PA_MIN);
  radio.setAutoAck(true);
  radio.startListening();

  // Set draw settings
  u8g2.setBitmapMode(1);
  u8g2.setDrawColor(2);
  u8g2.setFontMode(1);

  unsigned long seed = millis();

  // Gather entropy for a (hopefully) unique ID
  seed = (seed << 8) | analogRead(A1);
  seed = (seed << 8) | analogRead(A6);
  seed = (seed << 8) | analogRead(A7);

  randomSeed(seed);

  uniqueID = random(65535);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> LOOP <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void loop() {
  unsigned long currentMillis = millis();
  checkGameState(currentMillis);

  if (!currentGD.disabled) {
    ReadReloadButton(currentMillis);
    handleTrigger(currentMillis);
  }

  if (currentGD.phase == GamePhase::GameInProgress) {
    handleIRReception();

    if (currentGD.isReloading
        && (currentGD.ammo < maxAmmo
          || (currentGD.ultraBeamRemaining > 0
            && !currentGD.ultraBeamCharged))) {
      UpdateAmmo(true, currentMillis);
    }
  }

  recieveData();
  if (currentGD.phase != GamePhase::PressStart) {
    heartBeat(currentMillis);
  }

  updateDisplay(currentMillis);
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> UI FUNCTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// Draw screen only as needed to minimize processing time
void updateDisplay(unsigned long currentMillis) {
  if (pageDone) {
    // If page is done drawing, check to see if it needs updating
    if (!gameDataEquals(drawGD, currentGD)
        || currentMillis - timeoutStartTime < HIT_TIMEOUT
        || currentGD.isReloading
        || (currentGD.emp > 0 && !buttonWasReleased && currentGD.isReloading && currentMillis - previousTriggerMillis <= EMP_HOLD)
        || currentGD.phase == GamePhase::GameCountdown
        || currentGD.phase == GamePhase::TeamSelect) {

      chargingUltraBeam = false;

      if (currentGD.ammo < maxAmmo && currentGD.isReloading) {
        reloadArc = max(255 - (currentMillis - reloadTimer) * 255 / RELOAD_TIME_EACH, 0);
      } else if (currentGD.ammo == maxAmmo && currentGD.isReloading && currentGD.ultraBeamRemaining > 0) {
        reloadArc = max(255 - (currentMillis - reloadTimer) * 255 / CHARGE_ULTRA_BEAM, 0);
        chargingUltraBeam = true;
      }

      countdownBox = 0;
      if (hitTimeout) {
        countdownBox = min((float)(currentMillis - timeoutStartTime) / HIT_TIMEOUT, 1.0);
      } else if (currentGD.phase == GamePhase::TeamSelect && !buttonWasReleased) {
        countdownBox = min((float)(currentMillis - previousTriggerMillis) / START_GAME_HOLD, 1.0);
      }

      drawGD = currentGD;

      chargingEMP = false;
      if (currentGD.emp > 0 && currentGD.isReloading && !buttonWasReleased && currentMillis - previousTriggerMillis <= EMP_HOLD) {
        chargingEMP = true;
        drawGD.emp = currentGD.emp - 1;
        empRadius = min((float)(currentMillis - previousTriggerMillis) / EMP_HOLD, 1.0);
      }

      if (currentGD.phase == GamePhase::GameCountdown) {
        gameStartCountdown = 3 - (currentMillis - gameInitiatedTime) / 1000;
      }

      pageDone = false;
    }
    if (!pageDone) {
      u8g2.firstPage();
    }
  }
  if (!pageDone) {

    if (drawGD.phase != GamePhase::PressStart) {
      drawStatus();
    }

    if(currentGD.phase == GamePhase::TeamSelect) {
      drawButtonHoldBox();
    }

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setFontPosTop();

    if (drawGD.phase == GamePhase::PressStart) {
      drawPressStart();
    } else if (drawGD.phase == GamePhase::GameCountdown) {
      drawGameStartCountdown();
    } else if (drawGD.phase == GamePhase::TeamSelect) {
      drawTeamSelect();
    } else if (drawGD.phase == GamePhase::GameOver) {
      drawGameOver();
    } else if (drawGD.hitTeam > 0 || drawGD.hitUltraBeam > 0) {
      drawHitNotification();
    } else if (drawGD.ultraBeamCharged) {
      drawUltraBeamCharged();
    } else {
      drawAmmo();
      drawHitIndicator();
    }

    if (drawGD.phase != GamePhase::PressStart && !displayError) {
      drawHealth();
    } else if (displayError) {
      u8g2.setCursor(0, 48);
      u8g2.setFont(u8g2_font_helvR08_tr);
      u8g2.print("E: ");
      u8g2.print(errorMsg);
    }

    if (!u8g2.nextPage()) {
      pageDone = true;
    }
  }
}

bool gameDataEquals(const GameData& s1, const GameData& s2) {
  return s1.ammo == s2.ammo
      && s1.emp == s2.emp
      && s1.health == s2.health
      && s1.lives == s2.lives
      && s1.hitTeam == s2.hitTeam
      && s1.hitUltraBeam == s2.hitUltraBeam
      && s1.ultraBeamRemaining == s2.ultraBeamRemaining
      && s1.team1PlayerCount == s2.team1PlayerCount
      && s1.team2PlayerCount == s2.team2PlayerCount
      && s1.team3PlayerCount == s2.team3PlayerCount
      && s1.shielded == s2.shielded
      && s1.disabled == s2.disabled
      && s1.isReloading == s2.isReloading
      && s1.ultraBeamCharged == s2.ultraBeamCharged
      && s1.hitIndicator == s2.hitIndicator
      && s1.phase == s2.phase;
}

void drawStatus() {
  u8g2.setCursor(118, 0);
  u8g2.setFont(u8g2_font_helvR08_tr);
  u8g2.setFontPosTop();
  u8g2.print("T");
  u8g2.print(drawGD.team);

  for (uint8_t x = 0; x < drawGD.lives; x++) {
    u8g2.drawXBMP(0 + x * 11, 0, 16, 8, heart_bitmap);
  }
}

void drawButtonHoldBox() {
  u8g2.drawBox(0, 0, 128 * countdownBox, 48);
}

void drawPressStart() {
  u8g2.setCursor(32, 9);
  u8g2.print("PRESS");
  u8g2.setCursor(32, 27);
  u8g2.print("START");
}

void drawTeamSelect() {
  u8g2.setFont(u8g2_font_helvR08_tr);
  u8g2.setCursor(10, 9);
  u8g2.print("Select Team");

  const uint8_t offset = 10;
  for (uint8_t i = 1; i <= 3; i++) {
    u8g2.setCursor(10, 9 + i * offset);
    u8g2.print("T");
    u8g2.print(i);

    uint8_t teamCount = getTeamCount(drawGD, i-1);
    teamCount -= drawGD.team == i ? 1 : 0;
    if (teamCount >= MAX_TEAM_PLAYERS) {
      u8g2.setCursor(30, 9 + i * offset);
      u8g2.print("FULL");
    } else {
      for (uint8_t j = 0; j < teamCount; j++) {
        u8g2.drawXBMP(23 + j * 8, 10 + i * offset, 8, 8, remote_player_bitmap);
      }
    }
  }

  u8g2.drawXBMP(3, 10 + drawGD.team * offset, 8, 8, player_bitmap);
}

void drawHitNotification() {
  // Cooldown bar
  u8g2.drawBox(0, 0, 128 * (1.0 - countdownBox), 47);

  if (drawGD.hitTeam == drawGD.team) {
    u8g2.setCursor(10, 9);
    u8g2.print("OVERLOAD");
    u8g2.setCursor(20, 27);
    u8g2.print("DAMAGE");
  } else if (drawGD.hitUltraBeam > 0) {
    u8g2.setCursor(0, 9);
    u8g2.print("ULTRA BEAM");
    u8g2.setCursor(0, 27);
    u8g2.print("FROM TEAM ");
    u8g2.print(drawGD.hitUltraBeam);
  } else if (drawGD.disabled) {
    u8g2.setCursor(10, 9);
    u8g2.print("EMP BLAST");
    u8g2.setCursor(0, 27);
    u8g2.print("FROM TEAM ");
    u8g2.print(drawGD.hitTeam);
  } else {
    u8g2.setCursor(20, 9);
    u8g2.print("HIT FROM");
    u8g2.setCursor(20, 27);
    u8g2.print("TEAM ");
    u8g2.print(drawGD.hitTeam);
  }
}

void drawUltraBeamCharged() {
  u8g2.setCursor(1, 9);
  u8g2.print("ULTRA BEAM");
  u8g2.setCursor(17, 27);
  u8g2.print("CHARGED");
}

void drawGameStartCountdown() {
  if(gameStartCountdown == 0) {
    u8g2.setCursor(27, 22);
    u8g2.print("START!");
  } else {
    u8g2.setFont(u8g2_font_helvB24_tn);
    u8g2.setFontPosCenter();
    u8g2.setCursor(64 - numHalfWidth[gameStartCountdown], 31);
    u8g2.print(gameStartCountdown);
  }
}

void drawAmmo() {
  u8g2.setFontPosCenter();
  u8g2.setCursor(64 - numHalfWidth[drawGD.ammo], 31);
  u8g2.setFont(u8g2_font_helvB24_tn);
  u8g2.print(drawGD.ammo);


  // EMP Radius
  if (chargingEMP) {
    u8g2.drawCircle(96, 27, 17);
    u8g2.drawDisc(96, 27, 3 + empRadius * 15);
    u8g2.drawXBMP(92, 23, 8, 8, emp_bitmap);
  } else {
    // Reload Arc
    if (drawGD.isReloading && drawGD.ammo < maxAmmo || chargingUltraBeam) {
      u8g2.drawArc(64, 27, 20, reloadArc, 0);
      if (chargingUltraBeam) {
        u8g2.drawArc(64, 27, 18, reloadArc, 0);
        u8g2.drawArc(64, 27, 16, reloadArc, 0);
      }
    }
  }

  for (uint8_t x = 0; x < drawGD.ultraBeamRemaining; x++) {
    u8g2.drawXBMP(x * 11, 39, 8, 8, ub_bitmap);
  }

  for (uint8_t x = 0; x < drawGD.emp; x++) {
    u8g2.drawXBMP(120 - x * 11, 39, 8, 8, emp_bitmap);
  }
}

void drawHitIndicator() {
  if (drawGD.hitIndicator == HitIndicator::Hit) {
    u8g2.drawLine(64, 0, 64, 9);
    u8g2.drawLine(46, 27, 37, 27);
    u8g2.drawLine(82, 27, 91, 27);
  } else if (drawGD.hitIndicator == HitIndicator::UltraBeamHit) {
    u8g2.drawLine(63, 0, 63, 9);
    u8g2.drawLine(65, 0, 65, 9);
    u8g2.drawLine(46, 27, 37, 27);
    u8g2.drawLine(46, 29, 37, 29);
    u8g2.drawLine(82, 27, 91, 27);
    u8g2.drawLine(82, 29, 91, 29);
  } else if (drawGD.hitIndicator == HitIndicator::Elimination) {
    u8g2.drawLine(64, 0, 60, 10);
    u8g2.drawLine(64, 0, 68, 10);
    u8g2.drawLine(46, 27, 37, 27);
    u8g2.drawLine(46, 32, 37, 27);
    u8g2.drawLine(82, 27, 91, 27);
    u8g2.drawLine(82, 32, 91, 27);
  }
}

void drawHealth() {
  if (drawGD.shielded) {
    u8g2.drawTriangle(0,48, 40,48, 0,53);
    u8g2.drawTriangle(40,51, 40,48, 0,53);
  }
  if (drawGD.health > 0) {
    u8g2.drawTriangle(0,55, 40,53, 0,63);
    u8g2.drawTriangle(40,63, 40,53, 0,63);
  }
  if (drawGD.health > 1) {
    u8g2.drawTriangle(43,53, 84,50, 43,63);
    u8g2.drawTriangle(84,63, 84,50, 43,63);
  }
  if (drawGD.health > 2) {
    u8g2.drawTriangle(87,50, 127,48, 87,63);
    u8g2.drawTriangle(127,63, 127,48, 87,63);
  }
}

void drawGameOver() {
  if (currentGD.victory) {
    u8g2.setCursor(18, 22);
    u8g2.print("VICTORY!");
  } else {
    u8g2.setCursor(35, 9);
    u8g2.print("GAME");
    u8g2.setCursor(35, 27);
    u8g2.print("OVER");
  }
}

void broadcast(PacketType type, uint8_t sendPlayerId = 0) {
  DataPacket data;
  data.uniqueID = uniqueID;
  data.type = type;
  data.playerId = sendPlayerId;

  radio.stopListening();
  radio.write(&data, sizeof(data));
  radio.startListening();
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>> GAMEPLAY FUNCTIONS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// Read Trigger ----------
void handleTrigger(unsigned long currentMillis) {
  bool triggerPressed = ReadTriggerButton();

  if (triggerPressed && buttonWasReleased && currentGD.ammo > 0 && currentMillis - previousTriggerMillis >= TRIGGER_COOLDOWN) {
    previousTriggerMillis = currentMillis;
    buttonWasReleased = false;

    if (currentGD.phase == GamePhase::GameInProgress && !currentGD.isReloading) {
      buzzFor(200);
      sendIR_Pulse();
      UpdateAmmo(false, currentMillis);
    } else if (currentGD.phase == GamePhase::PressStart) {
      if (changeTeam()) {
        currentGD.phase = GamePhase::TeamSelect;
      }
    }
  } else if (!triggerPressed) {
    buttonWasReleased = true;
  } else if (currentGD.phase == GamePhase::TeamSelect && !buttonWasReleased) {
    if (currentMillis - previousTriggerMillis >= START_GAME_HOLD) {
      broadcast(PacketType::StartGame);
      broadcast(PacketType::StartGame);
      initiateGame();
      releaseTriggerButton();
    }
  } else if (currentGD.emp > 0 && currentGD.isReloading && currentGD.phase == GamePhase::GameInProgress && !buttonWasReleased && !currentGD.ultraBeamCharged) {
    if (currentMillis - previousTriggerMillis >= EMP_HOLD) {
      broadcast(PacketType::EMP, playerId);
      buzzFor(200);
      currentGD.emp -= 1;
      applyFriendlyEMP();
      releaseTriggerButton();
      currentGD.isReloading = false;
      reloadTimer = currentMillis;
    }
  }
}

void releaseTriggerButton() {
  buttonWasReleased = true;
  previousTriggerMillis = millis();
}

void buzzFor(unsigned long time) {
  buzzerTime = millis() + time;
  digitalWrite(BUZZER_PIN, HIGH);
}

// Fire "Shot" ----------
void sendIR_Pulse() {
  Serial.flush();
  if (currentGD.ultraBeamCharged) {
    IrSender.sendNEC(0x00, playerId + 1, 5);
    markHit(currentGD.team); // Self hit
  } else {
    IrSender.sendNEC(0x00, playerId, 3);
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
  if (IrReceiver.decodedIRData.command == 0) {
    return;
  }
  uint8_t fromTeam = getTeamIndex(IrReceiver.decodedIRData.command) + 1;
  if (fromTeam != currentGD.team) {
    if (millis() - timeoutStartTime > HIT_TIMEOUT + 1000) {
      markHit(fromTeam);
    }
  }
}

// Move goggles if hit ----------
void markHit(uint8_t fromTeam) {
  timeoutStartTime = millis();
  hitTimeout = true;
  currentGD.disabled = false;

  // Odd commands are ultra beam shots
  bool ultraBeamHit = (IrReceiver.decodedIRData.command % 2 == 1) 
                   && (fromTeam != currentGD.team);
  uint8_t damage = ultraBeamHit ? 3 : 1;

  if (currentGD.shielded) {
    currentGD.shielded = false;
    damage -= 1;
    if(damage == 0) {
      buzzFor(200);
      return;
    }
  }

  if (fromTeam == currentGD.team) {
    // Self hit from ultra beam
    currentGD.hitTeam = currentGD.team;
  } else if (ultraBeamHit) {
    currentGD.hitUltraBeam = fromTeam;
    currentGD.emp = min(currentGD.emp + 1, maxEMP);
  } else {
    currentGD.hitTeam = fromTeam;
  }

  // move goggles to darken
  myservo.attach(SERVO_PIN);

  uint8_t prevHealth = currentGD.health;
  currentGD.health = max(0, currentGD.health - damage);

  myservo.write(SERVO_HIT_POS);
  unsigned long servoStartTime = millis();
  int runTime = 0;
  if (prevHealth - currentGD.health == 1) {
    runTime = SERVO_TIME_TO_33;
  } else if (prevHealth - currentGD.health == 2) {
    runTime = SERVO_TIME_TO_66;
  } else if (prevHealth - currentGD.health >= 3) {
    runTime = SERVO_TIME_TO_100;
  }

  if (currentGD.health <= 0) {
    runTime + 300;
    currentGD.lives -= 1;
  }

  if (currentGD.lives == 0) {
    eliminated = true;
    currentGD.phase = GamePhase::GameOver;

    eliminatedMe = IrReceiver.decodedIRData.command;

    broadcast(PacketType::ImEliminated, IrReceiver.decodedIRData.command);
  } else if (fromTeam != currentGD.team) {
    uint8_t playerIndex = getPlayerIndex(IrReceiver.decodedIRData.command);
    broadcast(PacketType::ImHit, IrReceiver.decodedIRData.command);
  }

  digitalWrite(BUZZER_PIN, HIGH);

  // Vibrate buzzer during timeout. !!! section is blocking !!!
  while (millis() - timeoutStartTime < HIT_TIMEOUT) {
    if (millis() - servoStartTime > runTime) {
      myservo.write(SERVO_STOP);

      // End hit timeout early if self damage
      if(fromTeam == currentGD.team) {
        break;
      }
    }

    // In last 20% of timeout, begin to move servo towards starting position
    int timeVal = (millis() - timeoutStartTime) / 100;
    if (currentGD.health == 0 && millis() > timeoutStartTime + (HIT_TIMEOUT * (4.0 / 5.0))) {
      myservo.write(SERVO_INITIAL_POS);
    }

    recieveData();
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
  currentGD.hitTeam = 0;
  currentGD.hitUltraBeam = 0;

  currentGD.isReloading = false;
  if (currentGD.health <= 0 && currentGD.phase != GamePhase::GameOver) {
    if (deathTakesAmmo) {
      currentGD.ammo = 0;
    }
    currentGD.health = 3;
  }

  myservo.detach();
}

// >>>>>>>>>>>>>>>>>>>>> RUN UI COMPONENTS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void UpdateAmmo(bool dir, unsigned long currentMillis) {
  if (currentGD.ammo > maxAmmo) {
    currentGD.ammo = maxAmmo;
    if (currentGD.ultraBeamRemaining > 0) {
      currentGD.isReloading = false;
    }
  }
  if (!dir) {
    if (currentGD.ultraBeamCharged) {
      currentGD.ammo = 0;
      currentGD.ultraBeamCharged = false;
    } else {
      currentGD.ammo -= 1;
    }
  } else {
    if(buttonWasReleased) {
      if (currentMillis - reloadTimer > RELOAD_TIME_EACH && currentGD.ammo < maxAmmo) {
        currentGD.ammo += 1;
        reloadTimer = currentMillis;
      }
      if (currentMillis - reloadTimer > CHARGE_ULTRA_BEAM && currentGD.ultraBeamRemaining > 0) {
        currentGD.ultraBeamCharged = true;
        currentGD.ultraBeamRemaining -= 1;
      }
    }
  }
}

// >>>>>>>>>>>>>>>>>>>>>>> BUTTON DEBOUNCE FILTERS <<<<<<<<<<<<<<<<<<<<<<<<<
bool ReadTriggerButton() {
  uint8_t triggerVal = digitalRead(TRIGGER_PIN);
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
  uint8_t reloadVal = digitalRead(RELOAD_PIN);
  if (reloadVal != lastReloadVal) {
    lastRDebounceTime = currentMillis;
  }
  if ((currentMillis - lastRDebounceTime) > DEBOUNCE_DELAY) {
    if (reloadVal != reloadState) {
      reloadState = reloadVal;
      if (reloadState == LOW) {
        reloadTimer = currentMillis;

        if(currentGD.phase == GamePhase::TeamSelect) {
          changeTeam();
        }
        if (currentGD.phase == GamePhase::PressStart) {
          if (changeTeam()) {
            currentGD.phase = GamePhase::TeamSelect;
          }
        }
      }
    }
  }
  lastReloadVal = reloadVal;
  if (reloadState == LOW) {
    currentGD.isReloading = true;
  } else {
    currentGD.isReloading = false;
  }
}

bool changeTeam() {
  bool teamSelected = false;
  uint8_t teamAttempts = 3;
  while (!teamSelected) {
    currentGD.team = currentGD.team % 3 + 1;
    if(getTeamCount(currentGD, currentGD.team - 1) <= MAX_TEAM_PLAYERS) {
      uint8_t idAttempts = MAX_TEAM_PLAYERS;
      while (idAttempts > 0) {
        if (getNewPlayerId()) {
          teamSelected = true;
          break;
        }
        idAttempts -= 1;
      }
    } else {
      teamAttempts -= 1;
      if (teamAttempts == 0) {
        currentGD.phase = GamePhase::PressStart;
        displayError = true;
        strcpy(errorMsg, "All teams full");
        return false;
      }
    }
  }
  return true;
}

bool getNewPlayerId() {
  bool idSelected = false;
  unsigned int timeIndex = millis();
  uint8_t attempts = MAX_TEAM_PLAYERS;

  if (getTeamCount(currentGD, currentGD.team - 1) == 0) {
    return false;
  }

  while (!idSelected) {
    playerId = ((timeIndex % MAX_TEAM_PLAYERS) * 2) + ((currentGD.team - 1) * ID_RANGE) + ID_OFFSET;
    playerIdx = getPlayerIndex(playerId);
    if (registeredPlayers[currentGD.team - 1][playerIdx].uniqueID == 0) {
      registeredPlayers[currentGD.team - 1][playerIdx].uniqueID = uniqueID;
      idSelected = true;
    }
    timeIndex += 1;
    attempts -= 1;
    if (attempts <= 0) {
      return false;
    }
  }

  broadcast(PacketType::IAm, playerId);
  return true;
}

void recieveData() {
  if (radio.available()) {
    DataPacket data;
    data.type = PacketType::Undefined;
    radio.read(&data, sizeof(data));
    uint8_t teamIdx = getTeamIndex(data.playerId);
    uint8_t otherPlayerIdx = getPlayerIndex(data.playerId);

    // Add, change or refresh Player ID
    if (data.type == PacketType::IAm || data.type == PacketType::IAmError) {
      if (data.type == PacketType::IAm) {
        // Is player already registered?
        if (registeredPlayers[teamIdx][otherPlayerIdx].uniqueID == data.uniqueID) {
          registeredPlayers[teamIdx][otherPlayerIdx].lastHeartbeat = millis() / 1000;
          return;

        // Is player trying to claim my ID?
        } else if (teamIdx == currentGD.team - 1 && otherPlayerIdx == playerIdx) {
          broadcast(PacketType::IAmError, playerId);
          return;
        }
      }

      // Move the player if they are already registered on another team
      PlayerData tempPD;
      movePrevPD(tempPD, data.uniqueID);

      // Register the player
      registeredPlayers[teamIdx][otherPlayerIdx] = tempPD;
      registeredPlayers[teamIdx][otherPlayerIdx].lastHeartbeat = millis() / 1000;

      // Update team player count
      addTeamPlayer(teamIdx, 1);

      // If somone sent an ID error for my ID, get a new ID
      if (data.type == PacketType::IAmError && data.playerId == playerId) {
        if (!getNewPlayerId()) {
          changeTeam();
        }
      }

    // Remote player started the game
    } else if (data.type == PacketType::StartGame && currentGD.phase == GamePhase::TeamSelect) {
      initiateGame();

    // Remote player set off an EMP
    } else if (data.type == PacketType::EMP) {
      if (teamIdx == currentGD.team - 1) {
        applyFriendlyEMP();
      } else {
        applyHostileEMP(teamIdx + 1);
      }

    // Remote player says they were hit
    } else if (data.type == PacketType::ImHit) {
      if (data.playerId == playerId) {
        currentGD.hitIndicator = HitIndicator::Hit;
        hitIndicatorTime = millis();
      } else if (data.playerId == playerId + 1) {
        currentGD.hitIndicator = HitIndicator::UltraBeamHit;
        hitIndicatorTime = millis();
      }

    // Remote player says they were Eliminated
    } else if (data.type == PacketType::ImEliminated) {
      if (data.playerId == playerId || data.playerId == playerId + 1) {
        currentGD.hitIndicator = HitIndicator::Elimination;
        eliminatePlayer(data.uniqueID);
        hitIndicatorTime = millis();
      }
    }
  }
}

void movePrevPD(PlayerData& tempPD, unsigned int newUniqueID) {
  tempPD = PlayerData{};
  tempPD.uniqueID = newUniqueID;
  for (uint8_t team = 0; team < 3; team++) {
    for (uint8_t i = 0; i < MAX_TEAM_PLAYERS; i++) {
      if (newUniqueID > 0 && registeredPlayers[team][i].uniqueID == newUniqueID) {
        tempPD = registeredPlayers[team][i];
        registeredPlayers[team][i] = PlayerData{};
        addTeamPlayer(team, -1);
      }
    }
  }

  return tempPD;
}

void eliminatePlayer(unsigned int elimUniqueID) {
  int8_t teamLeftStanding = -1;
  int8_t teamsLeftStanding = 0;
  for (uint8_t team = 0; team < 3; team++) {
    for (uint8_t i = 0; i < MAX_TEAM_PLAYERS; i++) {
      if (registeredPlayers[team][i].uniqueID == elimUniqueID) {
        registeredPlayers[team][i].eliminated = true;
        addTeamPlayer(team, -1);
      }
    }
    if (getTeamCount(currentGD, team) > 0) {
      teamLeftStanding = team;
      teamsLeftStanding += 1;
    }
  }
  if (teamsLeftStanding == 1 && teamLeftStanding == currentGD.team - 1) {
    currentGD.victory = true;
    currentGD.phase = GamePhase::GameOver;
  }
}

void addTeamPlayer(uint8_t team, int8_t quantity) {
  if(team == 0) {
    currentGD.team1PlayerCount = max(0, currentGD.team1PlayerCount + quantity);
  } else if(team == 1) {
    currentGD.team2PlayerCount = max(0, currentGD.team2PlayerCount + quantity);
  } else {
    currentGD.team3PlayerCount = max(0, currentGD.team3PlayerCount + quantity);
  }
}

uint8_t getTeamCount(const GameData& gd, uint8_t team) {
  uint8_t self = (gd.team == team + 1 && !eliminated) ? 1 : 0;
  if(team == 0) {
    return gd.team1PlayerCount + self;
  } else if(team == 1) {
    return gd.team2PlayerCount + self;
  } else {
    return gd.team3PlayerCount + self;
  }
}

uint8_t getPlayerIndex(uint8_t playerId) {
  uint8_t team = getTeamIndex(playerId);

  if (playerId % 2 == 1) {
    playerId -= 1;
  }

  return (playerId - ID_OFFSET - (team * ID_RANGE)) / 2;
}

uint8_t getTeamIndex(uint8_t playerId) {
  if (playerId - ID_OFFSET < ID_RANGE) {
    return 0;
  } else if (playerId - ID_OFFSET < ID_RANGE * 2) {
    return 1;
  } else {
    return 2;
  }
}

void heartBeat(unsigned long currentMillis) {
  if (currentMillis - lastHeartbeat < randomDelay) {
    return;
  }
  broadcast(PacketType::IAm, playerId);

  lastHeartbeat = currentMillis;
  randomDelay = random(1000, 2000);
}

void initiateGame() {
  gameInitiatedTime = millis();
  currentGD.phase = GamePhase::GameCountdown;
  buzzFor(200);
}

void applyFriendlyEMP() {
  currentGD.shielded = true;
  shieldStartTime = millis();
  buzzFor(200);
}

void applyHostileEMP(uint8_t team) {
  currentGD.disabled = true;
  currentGD.hitTeam = team;
  currentGD.isReloading = false;
  disabledStartTime = millis();
  releaseTriggerButton();
  buzzFor(500);
}

void checkGameState(unsigned long currentMillis) {
  if (currentGD.phase == GamePhase::GameInProgress) {
    if (currentGD.hitIndicator != HitIndicator::None && currentMillis - hitIndicatorTime > HIT_INDICATOR) {
      currentGD.hitIndicator = HitIndicator::None;
    }

    if (currentGD.disabled && currentMillis - disabledStartTime > DISABLED_TIMEOUT) {
      currentGD.disabled = false;
      currentGD.hitTeam = 0;
    }

    if (currentGD.shielded && currentMillis - shieldStartTime > SHIELD_TIMER) {
      currentGD.shielded = false;
    }
  } else if (currentGD.phase == GamePhase::GameCountdown) {
    if (currentMillis - gameInitiatedTime > START_GAME_COUNTDOWN) {
      currentGD.phase = GamePhase::GameInProgress;
      buzzFor(500);
    }
  }

  if (currentMillis > buzzerTime) {
    digitalWrite(BUZZER_PIN, LOW);
  }
}