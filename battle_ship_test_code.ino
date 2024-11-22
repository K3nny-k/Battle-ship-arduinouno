// Optimized Arduino Code: Battleship Game with 12x12 LED Matrices and LCD

#include <FastLED.h>
#include <LiquidCrystal_I2C.h>

// ==============================
// Pin Definitions
// ==============================
#define BUZZER_PIN 8
#define JOYSTICK_X_PIN A3
#define JOYSTICK_Y_PIN A1
#define JOYSTICK_BUTTON_PIN 2  // Joystick button pin (for orientation change)
#define RED_BUTTON_PIN 12      // For placing ships and confirming attacks
#define BLUE_BUTTON_PIN 13     // For starting/resetting the game
#define CPU_SIGNAL_PIN 7       // Signal pin to CPU Arduino

// ==============================
// LED Matrix Definitions
// ==============================
#define MATRIX_SIZE 12
#define NUM_LEDS (MATRIX_SIZE * MATRIX_SIZE) // 12x12 grid
#define DATA_PIN_PLAYER 3
#define DATA_PIN_CPU 5

CRGB ledsPlayer[NUM_LEDS];
CRGB ledsCPU[NUM_LEDS];

// ==============================
// LCD Definition
// ==============================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Initialize LCD with I2C address 0x27 and 16x2 size

// ==============================
// Game Constants
// ==============================
#define GRID_SIZE 10
#define NUM_SHIPS 5  // Number of ships

// Cell state encoding (2 bits per cell)
#define CELL_EMPTY 0b00
#define CELL_SHIP  0b01
#define CELL_HIT   0b10
#define CELL_MISS  0b11

// ==============================
// Ship Definitions (Stored in PROGMEM)
// ==============================
struct Ship {
  const char* name;
  uint8_t size;
};

const char carrierName[] PROGMEM = "Carrier";
const char battleshipName[] PROGMEM = "Battleship";
const char cruiserName[] PROGMEM = "Cruiser";
const char submarineName[] PROGMEM = "Submarine";
const char destroyerName[] PROGMEM = "Destroyer";

const Ship ships[NUM_SHIPS] PROGMEM = {
  {carrierName, 5},
  {battleshipName, 4},
  {cruiserName, 3},
  {submarineName, 3},
  {destroyerName, 2}
};

// ==============================
// Grid Representations (Bitfields)
// ==============================
const uint16_t totalGridBits = GRID_SIZE * GRID_SIZE * 2; // 200 bits
const uint16_t totalGridBytes = (totalGridBits + 7) / 8;  // 25 bytes

uint8_t playerGrid[totalGridBytes];
uint8_t arduinoGrid[totalGridBytes];
uint8_t playerAttackGrid[totalGridBytes];
uint8_t arduinoAttackGrid[totalGridBytes];

// ==============================
// Cursor Position
// ==============================
uint8_t cursorX = 0;
uint8_t cursorY = 0;

// ==============================
// Button State Variables
// ==============================
uint8_t redButtonState = HIGH;
uint8_t lastRedButtonState = HIGH;
uint8_t blueButtonState = HIGH;
uint8_t lastBlueButtonState = HIGH;

// Debounce timers
unsigned long lastRedDebounceTime = 0;
const unsigned long redDebounceDelay = 50; // 50 ms

unsigned long blueButtonPressTime = 0;
bool blueButtonLongPressHandled = false;
bool blueButtonShortPress = false;
const unsigned long blueDebounceDelay = 50; // 50 ms

// ==============================
// Joystick Variables
// ==============================
#define JOYSTICK_THRESHOLD 200
#define JOYSTICK_CENTER 512
unsigned long lastJoystickMoveTime = 0;
const unsigned long JOYSTICK_MOVE_DELAY = 200; // 200 ms

uint8_t joystickButtonState = HIGH;
uint8_t lastJoystickButtonState = HIGH;

// ==============================
// Game States
// ==============================
enum GameState : uint8_t { WAITING_TO_START, PLACING_SHIPS, PLAYER_TURN, ARDUINO_TURN, GAME_OVER };
GameState gameState = WAITING_TO_START;

// Orientation for ship placement
bool playerShipHorizontal = true; // true = horizontal

// Arduino attack mode
enum AttackMode : uint8_t { HUNT_MODE, TARGET_MODE };
AttackMode arduinoAttackMode = HUNT_MODE;

// Targeting variables for Arduino
uint8_t lastHitX;
uint8_t lastHitY;
uint8_t targetIndex;
uint8_t targetList[4][2]; // Adjacent cells
bool targetListInitialized = false;

// Ship placement tracker
uint8_t shipId = 0;

// ==============================
// Function Prototypes
// ==============================
void initializeGrids();
void placeArduinoShips();
void playerPlaceShips();
void playerAttack();
void arduinoAttack();
bool updateCursorPosition();
void playBuzzerTone(uint16_t frequency, uint16_t duration);
bool canPlaceShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal);
void placeShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal);
void resetGame();
uint8_t countRemainingShips(uint8_t* grid);
void updatePlayerMatrix();
void updateCPUMatrix();
bool isShipPreviewPosition(uint8_t x, uint8_t y);
int16_t getLEDIndex(int16_t x, int16_t y);
void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state);
uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y);
void handleJoystickButton();

// ==============================
// Setup Function
// ==============================
void setup() {
  // Initialize component pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CPU_SIGNAL_PIN, OUTPUT);
  digitalWrite(CPU_SIGNAL_PIN, LOW); // Ensure CPU signal is LOW at start

  // Initialize LED matrices
  FastLED.addLeds<WS2812B, DATA_PIN_PLAYER, GRB>(ledsPlayer, NUM_LEDS);
  FastLED.addLeds<WS2812B, DATA_PIN_CPU, GRB>(ledsCPU, NUM_LEDS);
  FastLED.setBrightness(20); // Set brightness to moderate level
  FastLED.clear(true);
  FastLED.show();

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Initialize Serial (optional, can be commented out to save memory)
  // Serial.begin(9600);
  // Serial.println(F("Battleship Game Initialized. Press Blue Button to Start."));

  // Display welcome message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Battleship Game"));
  lcd.setCursor(0, 1);
  lcd.print(F("Press Blue Btn"));
}

// ==============================
// Main Loop Function
// ==============================
void loop() {
  // ===========================
  // Blue Button Handling
  // ===========================
  blueButtonState = digitalRead(BLUE_BUTTON_PIN);
  if (blueButtonState != lastBlueButtonState) {
    if (blueButtonState == LOW) {
      // Button pressed
      blueButtonPressTime = millis();
      blueButtonLongPressHandled = false;
    } else {
      // Button released
      unsigned long pressDuration = millis() - blueButtonPressTime;
      if (pressDuration < 2000 && blueButtonPressTime != 0) {
        // Short press detected
        blueButtonShortPress = true;
      }
      blueButtonPressTime = 0;
    }
    lastBlueButtonState = blueButtonState;
  }

  // Handle long press
  if (blueButtonState == LOW && !blueButtonLongPressHandled && blueButtonPressTime != 0) {
    unsigned long pressDuration = millis() - blueButtonPressTime;
    if (pressDuration >= 2000) { // Long press threshold
      // Long press detected: Reset the game
      resetGame();
      playBuzzerTone(1000, 200); // Confirmation tone
      blueButtonLongPressHandled = true;
      blueButtonShortPress = false; // Prevent short press
    }
  }

  // Handle short press actions
  if (blueButtonShortPress) {
    blueButtonShortPress = false; // Reset flag
    if (gameState == WAITING_TO_START || gameState == GAME_OVER) {
      // Start or reset the game
      resetGame();
      playBuzzerTone(1000, 200); // Confirmation tone
    }
    // Other game states can handle short presses if needed
  }

  // ===========================
  // Game State Handling
  // ===========================
  switch (gameState) {
    case WAITING_TO_START:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      // Waiting for blue button press to start
      break;

    case PLACING_SHIPS:
      digitalWrite(CPU_SIGNAL_PIN, HIGH);  // Signal CPU Arduino (if applicable)
      playerPlaceShips();
      updatePlayerMatrix(); // Update the player's LED matrix
      break;

    case PLAYER_TURN:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      playerAttack();
      updateCPUMatrix(); // Update the CPU's LED matrix with attack results
      break;

    case ARDUINO_TURN:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      arduinoAttack();
      updatePlayerMatrix(); // Update the player's LED matrix with attack results
      break;

    case GAME_OVER:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      // Waiting for blue button press to restart
      break;
  }

  // Small delay to prevent flooding
  delay(50);
}

// ==============================
// Grid Initialization
// ==============================
void initializeGrids() {
  memset(playerGrid, 0, sizeof(playerGrid));
  memset(arduinoGrid, 0, sizeof(arduinoGrid));
  memset(playerAttackGrid, 0, sizeof(playerAttackGrid));
  memset(arduinoAttackGrid, 0, sizeof(arduinoAttackGrid));
}

// ==============================
// Game Reset Function
// ==============================
void resetGame() {
  // Initialize grids
  initializeGrids();

  // Place Arduino ships
  placeArduinoShips();

  // Reset cursor position
  cursorX = 0;
  cursorY = 0;

  // Reset button states
  lastRedButtonState = HIGH;
  lastBlueButtonState = HIGH;
  blueButtonPressTime = 0;
  blueButtonLongPressHandled = false;
  blueButtonShortPress = false;

  // Reset joystick button state
  lastJoystickButtonState = HIGH;

  // Reset orientation
  playerShipHorizontal = true;

  // Reset ship placement tracker
  shipId = 0;

  // Reset Arduino attack mode variables
  arduinoAttackMode = HUNT_MODE;
  lastHitX = 0;
  lastHitY = 0;
  targetIndex = 0;
  targetListInitialized = false;

  // Set game state to placing ships
  gameState = PLACING_SHIPS;

  // Display reset message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Place your ships"));
  lcd.setCursor(0, 1);
  lcd.print(F("Use Btns & Joy"));
}

// ==============================
// Arduino Ship Placement
// ==============================
void placeArduinoShips() {
  for (uint8_t shipIndex = 0; shipIndex < NUM_SHIPS; shipIndex++) {
    uint8_t shipSize = pgm_read_byte(&(ships[shipIndex].size));
    bool placed = false;

    while (!placed) {
      uint8_t x = random(0, GRID_SIZE);
      uint8_t y = random(0, GRID_SIZE);
      bool horizontal = random(0, 2); // 0 = vertical, 1 = horizontal

      if (canPlaceShip(arduinoGrid, x, y, shipSize, horizontal)) {
        placeShip(arduinoGrid, x, y, shipSize, horizontal);
        placed = true;
      }
    }
  }
}

// ==============================
// Player Ship Placement
// ==============================
void playerPlaceShips() {
  if (shipId >= NUM_SHIPS) {
    gameState = PLAYER_TURN;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Your Turn"));
    lcd.setCursor(0, 1);
    lcd.print(F("Attack CPU"));
    return;
  }

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  // Update cursor position based on joystick
  bool cursorMoved = updateCursorPosition();

  // Handle red button for placing ships
  uint8_t redReading = digitalRead(RED_BUTTON_PIN);
  if (redReading != lastRedButtonState) {
    lastRedDebounceTime = millis();
  }

  if ((millis() - lastRedDebounceTime) > redDebounceDelay) {
    if (redReading != redButtonState) {
      redButtonState = redReading;

      if (redButtonState == LOW) {
        // Attempt to place ship
        if (canPlaceShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal)) {
          placeShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal);
          shipId++;
          playBuzzerTone(1000, 200); // Confirmation tone

          // Retrieve ship name from PROGMEM
          char shipName[12];
          strcpy_P(shipName, currentShip.name);

          // Display placement confirmation on LCD
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Placed "));
          lcd.print(shipName);
          lcd.setCursor(0, 1);
          lcd.print(F("X:"));
          lcd.print(cursorX);
          lcd.print(F(" Y:"));
          lcd.print(cursorY);
          lcd.print(playerShipHorizontal ? " H" : " V"); // Orientation
        } else {
          // Cannot place ship here
          playBuzzerTone(500, 200); // Error tone
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Cannot Place"));
          lcd.setCursor(0, 1);
          lcd.print(F("Here"));
          delay(500);
        }
      }
    }
  }
  lastRedButtonState = redReading;

  // Handle orientation change with joystick button
  handleJoystickButton();

  // If cursor moved, update the LCD display
  if (cursorMoved) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Place "));
    // Retrieve ship name from PROGMEM
    char shipName[12];
    strcpy_P(shipName, currentShip.name);
    lcd.print(shipName);
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(cursorX);
    lcd.print(F(" Y:"));
    lcd.print(cursorY);
    lcd.print(playerShipHorizontal ? " H" : " V"); // Orientation
  }

  // Update the player's LED matrix
  updatePlayerMatrix();
}

// ==============================
// Player Attack Phase
// ==============================
void playerAttack() {
  // Update cursor position based on joystick
  bool cursorMoved = updateCursorPosition();

  // Handle red button for confirming attacks
  uint8_t redReading = digitalRead(RED_BUTTON_PIN);
  if (redReading != lastRedButtonState) {
    lastRedDebounceTime = millis();
  }

  if ((millis() - lastRedDebounceTime) > redDebounceDelay) {
    if (redReading != redButtonState) {
      redButtonState = redReading;

      if (redButtonState == LOW) {
        // Attempt to attack
        if (getCellState(playerAttackGrid, cursorX, cursorY) == CELL_EMPTY) {
          uint8_t cellState = getCellState(arduinoGrid, cursorX, cursorY);
          if (cellState == CELL_SHIP) {
            // Hit
            setCellState(playerAttackGrid, cursorX, cursorY, CELL_HIT);
            setCellState(arduinoGrid, cursorX, cursorY, CELL_HIT);
            playBuzzerTone(1500, 500); // Hit tone

            // Display hit confirmation on LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Hit!"));
            lcd.setCursor(0, 1);
            lcd.print(F("X:"));
            lcd.print(cursorX);
            lcd.print(F(" Y:"));
            lcd.print(cursorY);

            // Check if player has won
            if (countRemainingShips(arduinoGrid) == 0) {
              gameState = GAME_OVER;
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print(F("You Win!"));
              lcd.setCursor(0, 1);
              lcd.print(F("Press Blue Btn"));
              playBuzzerTone(2000, 1000); // Victory tone
              return;
            }

            // Switch to Arduino's turn
            gameState = ARDUINO_TURN;
          } else {
            // Miss
            setCellState(playerAttackGrid, cursorX, cursorY, CELL_MISS);
            setCellState(arduinoGrid, cursorX, cursorY, CELL_MISS);
            playBuzzerTone(500, 200); // Miss tone

            // Display miss confirmation on LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Missed"));
            lcd.setCursor(0, 1);
            lcd.print(F("X:"));
            lcd.print(cursorX);
            lcd.print(F(" Y:"));
            lcd.print(cursorY);

            // Switch to Arduino's turn
            gameState = ARDUINO_TURN;
          }
        } else {
          // Already attacked this position
          playBuzzerTone(500, 200); // Error tone
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Already"));
          lcd.setCursor(0, 1);
          lcd.print(F("Attacked"));
          delay(500);
        }
      }
    }
  }
  lastRedButtonState = redReading;

  // If cursor moved, update the LCD display
  if (cursorMoved) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Attack at"));
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(cursorX);
    lcd.print(F(" Y:"));
    lcd.print(cursorY);
  }

  // Update the CPU's LED matrix
  updateCPUMatrix();
}

// ==============================
// Arduino Attack Phase
// ==============================
void arduinoAttack() {
  uint8_t x = 0, y = 0;
  bool validAttack = false;

  if (arduinoAttackMode == HUNT_MODE) {
    // Hunt Mode: Random attack
    do {
      x = random(0, GRID_SIZE);
      y = random(0, GRID_SIZE);
      if (getCellState(arduinoAttackGrid, x, y) == CELL_EMPTY) {
        validAttack = true;
      }
    } while (!validAttack);

    // Determine hit or miss
    uint8_t cellState = getCellState(playerGrid, x, y);
    if (cellState == CELL_SHIP) {
      // Hit
      setCellState(arduinoAttackGrid, x, y, CELL_HIT);
      setCellState(playerGrid, x, y, CELL_HIT);
      playBuzzerTone(1500, 500); // Hit tone

      // Display hit confirmation on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("CPU Hit!"));
      lcd.setCursor(0, 1);
      lcd.print(F("X:"));
      lcd.print(x);
      lcd.print(F(" Y:"));
      lcd.print(y);

      // Update targeting variables
      lastHitX = x;
      lastHitY = y;
      arduinoAttackMode = TARGET_MODE;
      targetIndex = 0;
      targetListInitialized = false;
    } else {
      // Miss
      setCellState(arduinoAttackGrid, x, y, CELL_MISS);
      setCellState(playerGrid, x, y, CELL_MISS);
      playBuzzerTone(500, 200); // Miss tone

      // Display miss confirmation on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("CPU Missed"));
      lcd.setCursor(0, 1);
      lcd.print(F("X:"));
      lcd.print(x);
      lcd.print(F(" Y:"));
      lcd.print(y);
    }
  } else if (arduinoAttackMode == TARGET_MODE) {
    // Target Mode: Attack adjacent cells
    if (!targetListInitialized) {
      // Initialize target list with adjacent cells
      targetIndex = 0;
      uint8_t idx = 0;

      // Left
      if (lastHitX > 0) {
        targetList[idx][0] = lastHitX - 1;
        targetList[idx][1] = lastHitY;
        idx++;
      }
      // Right
      if (lastHitX < GRID_SIZE - 1) {
        targetList[idx][0] = lastHitX + 1;
        targetList[idx][1] = lastHitY;
        idx++;
      }
      // Up
      if (lastHitY > 0) {
        targetList[idx][0] = lastHitX;
        targetList[idx][1] = lastHitY - 1;
        idx++;
      }
      // Down
      if (lastHitY < GRID_SIZE - 1) {
        targetList[idx][0] = lastHitX;
        targetList[idx][1] = lastHitY + 1;
        idx++;
      }

      targetListInitialized = true;
    }

    // Attempt to attack the next target in the list
    while (targetIndex < 4) {
      x = targetList[targetIndex][0];
      y = targetList[targetIndex][1];
      targetIndex++;

      if (getCellState(arduinoAttackGrid, x, y) == CELL_EMPTY) {
        validAttack = true;
        break;
      }
    }

    if (validAttack) {
      // Determine hit or miss
      uint8_t cellState = getCellState(playerGrid, x, y);
      if (cellState == CELL_SHIP) {
        // Hit
        setCellState(arduinoAttackGrid, x, y, CELL_HIT);
        setCellState(playerGrid, x, y, CELL_HIT);
        playBuzzerTone(1500, 500); // Hit tone

        // Display hit confirmation on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("CPU Hit!"));
        lcd.setCursor(0, 1);
        lcd.print(F("X:"));
        lcd.print(x);
        lcd.print(F(" Y:"));
        lcd.print(y);

        // Update targeting variables
        lastHitX = x;
        lastHitY = y;
        arduinoAttackMode = TARGET_MODE;
        targetIndex = 0;
        targetListInitialized = false;
      } else {
        // Miss
        setCellState(arduinoAttackGrid, x, y, CELL_MISS);
        setCellState(playerGrid, x, y, CELL_MISS);
        playBuzzerTone(500, 200); // Miss tone

        // Display miss confirmation on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("CPU Missed"));
        lcd.setCursor(0, 1);
        lcd.print(F("X:"));
        lcd.print(x);
        lcd.print(F(" Y:"));
        lcd.print(y);
      }
    } else {
      // No valid targets left, switch back to hunt mode
      arduinoAttackMode = HUNT_MODE;
      arduinoAttack(); // Retry attack in hunt mode
      return;
    }
  }

  // Check if Arduino has won
  if (countRemainingShips(playerGrid) == 0) {
    gameState = GAME_OVER;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("CPU Wins!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Press Blue Btn"));
    playBuzzerTone(100, 1000); // Defeat tone
    return;
  }

  // Switch back to player's turn
  gameState = PLAYER_TURN;
}

// ==============================
// Cursor Position Update
// ==============================
bool updateCursorPosition() {
  int16_t xValue = analogRead(JOYSTICK_X_PIN);
  int16_t yValue = analogRead(JOYSTICK_Y_PIN);
  unsigned long currentTime = millis();
  bool moved = false;

  if (currentTime - lastJoystickMoveTime > JOYSTICK_MOVE_DELAY) {
    // Left Movement
    if (xValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      if (cursorX > 0) {
        cursorX--;
        moved = true;
      }
      lastJoystickMoveTime = currentTime;
    }
    // Right Movement
    else if (xValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      if (cursorX < GRID_SIZE - 1) {
        cursorX++;
        moved = true;
      }
      lastJoystickMoveTime = currentTime;
    }

    // Up Movement (Y-axis reversed)
    if (yValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      if (cursorY > 0) {
        cursorY--;
        moved = true;
      }
      lastJoystickMoveTime = currentTime;
    }
    // Down Movement
    else if (yValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      if (cursorY < GRID_SIZE - 1) {
        cursorY++;
        moved = true;
      }
      lastJoystickMoveTime = currentTime;
    }
  }

  return moved;
}

// ==============================
// Joystick Button Handling (Orientation Toggle)
// ==============================
void handleJoystickButton() {
  joystickButtonState = digitalRead(JOYSTICK_BUTTON_PIN);

  if (joystickButtonState != lastJoystickButtonState) {
    if (joystickButtonState == LOW) {
      // Button pressed, toggle ship orientation
      playerShipHorizontal = !playerShipHorizontal;
      playBuzzerTone(800, 100); // Confirmation tone
    }
    lastJoystickButtonState = joystickButtonState;
  }
}

// ==============================
// Buzzer Tone Function
// ==============================
void playBuzzerTone(uint16_t frequency, uint16_t duration) {
  tone(BUZZER_PIN, frequency, duration);
}

// ==============================
// Ship Placement Validation
// ==============================
bool canPlaceShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal) {
  if (horizontal) {
    if (x + size > GRID_SIZE) return false;
    for (uint8_t i = 0; i < size; i++) {
      if (getCellState(grid, x + i, y) != CELL_EMPTY) return false;
    }
  } else {
    if (y + size > GRID_SIZE) return false;
    for (uint8_t i = 0; i < size; i++) {
      if (getCellState(grid, x, y + i) != CELL_EMPTY) return false;
    }
  }
  return true;
}

// ==============================
// Ship Placement Function
// ==============================
void placeShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal) {
  if (horizontal) {
    for (uint8_t i = 0; i < size; i++) {
      setCellState(grid, x + i, y, CELL_SHIP);
    }
  } else {
    for (uint8_t i = 0; i < size; i++) {
      setCellState(grid, x, y + i, CELL_SHIP);
    }
  }
}

// ==============================
// Cell State Management
// ==============================
void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t bitIndex = index * 2; // 2 bits per cell
  uint8_t byteIndex = bitIndex / 8;
  uint8_t bitOffset = bitIndex % 8;

  grid[byteIndex] &= ~(0b11 << bitOffset);       // Clear existing bits
  grid[byteIndex] |= ((state & 0b11) << bitOffset); // Set new state
}

uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t bitIndex = index * 2; // 2 bits per cell
  uint8_t byteIndex = bitIndex / 8;
  uint8_t bitOffset = bitIndex % 8;

  return (grid[byteIndex] >> bitOffset) & 0b11;
}

// ==============================
// Ship Counting Function
// ==============================
uint8_t countRemainingShips(uint8_t* grid) {
  uint8_t shipsRemaining = 0;
  for (uint8_t x = 0; x < GRID_SIZE; x++) {
    for (uint8_t y = 0; y < GRID_SIZE; y++) {
      if (getCellState(grid, x, y) == CELL_SHIP) {
        shipsRemaining++;
      }
    }
  }
  return shipsRemaining;
}

// ==============================
// LED Matrix Update Functions
// ==============================
void updatePlayerMatrix() {
  // Clear the player's LEDs
  fill_solid(ledsPlayer, NUM_LEDS, CRGB::Black);

  // Loop through the player's grid
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int16_t ledIndex = getLEDIndex(x, y);
      if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
        CRGB color = CRGB::Black; // Default

        // Highlight cursor position during ship placement
        if (gameState == PLACING_SHIPS && x == cursorX && y == cursorY) {
          color = CRGB::White;
        }

        // Determine cell state color
        uint8_t cellState = getCellState(playerGrid, x, y);
        if (cellState == CELL_SHIP) {
          color = CRGB::Green;
        } else if (cellState == CELL_HIT) {
          color = CRGB::Red;
        } else if (cellState == CELL_MISS) {
          color = CRGB::Blue;
        }

        // Ship preview during placement
        if (gameState == PLACING_SHIPS && isShipPreviewPosition(x, y)) {
          color = CRGB::Yellow;
        }

        ledsPlayer[ledIndex] = color;
      }
    }
  }

  FastLED.show();
}

void updateCPUMatrix() {
  // Clear the CPU's LEDs
  fill_solid(ledsCPU, NUM_LEDS, CRGB::Black);

  // Loop through the CPU's grid (player's attack grid)
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int16_t ledIndex = getLEDIndex(x, y);
      if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
        CRGB color = CRGB::Black; // Default

        // Highlight cursor position during player's turn
        if (gameState == PLAYER_TURN && x == cursorX && y == cursorY) {
          color = CRGB::White;
        }

        // Determine attack state color
        uint8_t attackState = getCellState(playerAttackGrid, x, y);
        if (attackState == CELL_HIT) {
          color = CRGB::Red;
        } else if (attackState == CELL_MISS) {
          color = CRGB::Blue;
        }

        ledsCPU[ledIndex] = color;
      }
    }
  }

  FastLED.show();
}

// ==============================
// Ship Preview Function
// ==============================
bool isShipPreviewPosition(uint8_t x, uint8_t y) {
  if (gameState != PLACING_SHIPS) return false;

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  uint8_t shipSize = currentShip.size;

  if (playerShipHorizontal) {
    if (cursorX + shipSize > GRID_SIZE) return false;
    for (uint8_t i = 0; i < shipSize; i++) {
      if (x == cursorX + i && y == cursorY) return true;
    }
  } else {
    if (cursorY + shipSize > GRID_SIZE) return false;
    for (uint8_t i = 0; i < shipSize; i++) {
      if (x == cursorX && y == cursorY + i) return true;
    }
  }

  return false;
}

// ==============================
// LED Index Mapping Function
// ==============================
int16_t getLEDIndex(int16_t x, int16_t y) {
  // Map 10x10 grid to 12x12 LED matrix (centered)
  // Grid (0-9, 0-9) maps to LED matrix (1-10, 1-10)
  if (x >= GRID_SIZE || y >= GRID_SIZE) return -1; // Invalid

  uint8_t ledX = x + 1; // Offset by 1
  uint8_t ledY = y + 1; // Offset by 1

  // Assuming serpentine wiring: even rows left-to-right, odd rows right-to-left
  if (ledY % 2 == 0) {
    return ledY * MATRIX_SIZE + ledX;
  } else {
    return ledY * MATRIX_SIZE + (MATRIX_SIZE - 1 - ledX);
  }
}
