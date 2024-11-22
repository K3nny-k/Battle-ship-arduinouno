// Optimized Arduino Code: Battleship Game with LCD and Memory Efficiency

#include <FastLED.h>
#include <LiquidCrystal_I2C.h>

// Define pins
#define BUZZER_PIN 8
#define JOYSTICK_X_PIN A3
#define JOYSTICK_Y_PIN A1
#define JOYSTICK_BUTTON_PIN 2  // Joystick button pin (for orientation change)
#define RED_BUTTON_PIN 12      // For placing ships and confirming attacks
#define BLUE_BUTTON_PIN 13     // For starting/resetting the game
#define CPU_SIGNAL_PIN 7       // Signal pin to CPU Arduino

// Game grid size
#define GRID_SIZE 10

// LED Matrix definitions
#define NUM_LEDS (GRID_SIZE * GRID_SIZE) // 10x10 grid
#define DATA_PIN_PLAYER 3
#define DATA_PIN_CPU 5

CRGB ledsPlayer[NUM_LEDS];
CRGB ledsCPU[NUM_LEDS];

// Game constants
#define NUM_SHIPS 5  // Number of ships

// Cell state encoding (2 bits per cell)
#define CELL_EMPTY 0b00
#define CELL_SHIP  0b01
#define CELL_HIT   0b10
#define CELL_MISS  0b11

// Ship definitions stored in PROGMEM
struct Ship {
  const char* name;
  uint8_t size;
};

// Store ship names in PROGMEM to save RAM
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

// Grids represented as bitfields (arrays of bytes)
const uint16_t totalGridBits = GRID_SIZE * GRID_SIZE * 2;
const uint16_t totalGridBytes = (totalGridBits + 7) / 8;

uint8_t playerGrid[totalGridBytes];
uint8_t arduinoGrid[totalGridBytes];
uint8_t playerAttackGrid[totalGridBytes];
uint8_t arduinoAttackGrid[totalGridBytes];

// Cursor position
uint8_t cursorX = 0;
uint8_t cursorY = 0;

// Button state variables
uint8_t redButtonState = HIGH;
uint8_t lastRedButtonState = HIGH;
uint8_t blueButtonState = HIGH;
uint8_t lastBlueButtonState = HIGH;

// Blue button long press variables
unsigned long blueButtonPressTime = 0;
bool blueButtonLongPressHandled = false;
bool blueButtonShortPress = false;
const unsigned long blueDebounceDelay = 50; // 50 ms debounce

// Debounce variables for red button
unsigned long lastRedDebounceTime = 0;
const unsigned long redDebounceDelay = 50; // 50 milliseconds debounce delay

// Joystick thresholds
#define JOYSTICK_THRESHOLD 200
#define JOYSTICK_CENTER 512
unsigned long lastJoystickMoveTime = 0;
#define JOYSTICK_MOVE_DELAY 200  // Delay between moves in ms

// Joystick button variables for orientation change
uint8_t joystickButtonState = HIGH;
uint8_t lastJoystickButtonState = HIGH;

// Game states
enum GameState : uint8_t { WAITING_TO_START, PLACING_SHIPS,
                           PLAYER_TURN, ARDUINO_TURN, GAME_OVER };
GameState gameState = WAITING_TO_START;

// Orientation variable for player's ships
bool playerShipHorizontal = true; // true = horizontal

// Arduino attack state variables
enum AttackMode : uint8_t { HUNT_MODE, TARGET_MODE };
AttackMode arduinoAttackMode = HUNT_MODE;

uint8_t lastHitX;
uint8_t lastHitY;
uint8_t targetIndex;
uint8_t targetList[4][2]; // Possible adjacent targets
bool targetListInitialized = false;

// Ship ID variable
uint8_t shipId = 0;

// Function prototypes
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

// Initialize LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); // Initialize LCD with I2C address 0x27 and 16x2 size

void setup() {
  // Initialize components
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CPU_SIGNAL_PIN, OUTPUT);
  digitalWrite(CPU_SIGNAL_PIN, LOW); // Ensure it's LOW at start

  // Initialize LEDs for Player and CPU
  FastLED.addLeds<WS2812B, DATA_PIN_PLAYER, GRB>(ledsPlayer, NUM_LEDS); // Controller 0
  FastLED.addLeds<WS2812B, DATA_PIN_CPU, GRB>(ledsCPU, NUM_LEDS);       // Controller 1
  FastLED.setBrightness(20); // Set global brightness
  FastLED.clear(true);
  FastLED.show();

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Initialize Serial for debugging (optional)
  // Serial.begin(9600); // Commented out to save memory
  // Serial.println(F("Battleship Game"));
  // Serial.println(F("Press the blue button to start."));

  // Display welcome message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Battleship Game"));
  lcd.setCursor(0, 1);
  lcd.print(F("Press Blue Btn"));
}

void loop() {
  // Read the blue button state
  blueButtonState = digitalRead(BLUE_BUTTON_PIN);

  // Check for blue button state change
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
      // Long press detected
      resetGame();
      playBuzzerTone(1000, 200);
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
      playBuzzerTone(1000, 200);
    }
    // Other states handle short presses in respective functions
  }

  // Handle game states
  switch (gameState) {
    case WAITING_TO_START:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      // Waiting for blue button press
      break;
    case PLACING_SHIPS:
      digitalWrite(CPU_SIGNAL_PIN, HIGH);  // Signal CPU Arduino
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
      // Waiting for blue button press
      break;
  }

  // Small delay to prevent flooding
  delay(50);
}

void resetGame() {
  // Initialize grids
  initializeGrids();

  // Place Arduino ships
  placeArduinoShips();

  // Reset cursor
  cursorX = 0;
  cursorY = 0;

  // Reset button states
  lastRedButtonState = HIGH;
  lastBlueButtonState = HIGH;
  blueButtonPressTime = 0;
  blueButtonLongPressHandled = false;
  blueButtonShortPress = false;

  // Reset joystick button states
  lastJoystickButtonState = HIGH;

  // Reset orientation
  playerShipHorizontal = true;

  // Reset shipId
  shipId = 0;

  // Reset Arduino attack mode variables
  arduinoAttackMode = HUNT_MODE;
  lastHitX = 0;
  lastHitY = 0;
  targetIndex = 0;
  targetListInitialized = false;

  // Set game state
  gameState = PLACING_SHIPS;

  // Display message on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Place your ships"));
  lcd.setCursor(0, 1);
  lcd.print(F("Use Btns & Joy"));

  // Update the LED matrices
  updatePlayerMatrix();
  updateCPUMatrix();
}

void initializeGrids() {
  // Initialize all grids to starting states
  memset(playerGrid, 0, sizeof(playerGrid));
  memset(arduinoGrid, 0, sizeof(arduinoGrid));
  memset(playerAttackGrid, 0, sizeof(playerAttackGrid));
  memset(arduinoAttackGrid, 0, sizeof(arduinoAttackGrid));
}

void placeArduinoShips() {
  // Place Arduino ships randomly
  for (uint8_t shipIndex = 0; shipIndex < NUM_SHIPS; shipIndex++) {
    uint8_t shipSize = pgm_read_byte(&(ships[shipIndex].size));
    bool placed = false;

    while (!placed) {
      uint8_t x = random(0, GRID_SIZE);
      uint8_t y = random(0, GRID_SIZE);
      bool horizontal = random(0, 2); // 0 = vertical, 1 = horizontal

      // Check if ship can be placed
      if (canPlaceShip(arduinoGrid, x, y, shipSize, horizontal)) {
        placeShip(arduinoGrid, x, y, shipSize, horizontal);
        placed = true;
      }
    }
  }
}

bool canPlaceShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal) {
  if (horizontal) {
    if (x + size > GRID_SIZE) return false;
    for (uint8_t i = 0; i < size; i++) {
      uint8_t xi = x + i;
      uint8_t yi = y;
      if (getCellState(grid, xi, yi) != CELL_EMPTY) return false;
    }
  } else {
    if (y + size > GRID_SIZE) return false;
    for (uint8_t i = 0; i < size; i++) {
      uint8_t xi = x;
      uint8_t yi = y + i;
      if (getCellState(grid, xi, yi) != CELL_EMPTY) return false;
    }
  }
  return true;
}

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

void playerPlaceShips() {
  bool displayUpdated = false;

  // Check if all ships have been placed
  if (shipId >= NUM_SHIPS) {
    gameState = PLAYER_TURN;
    // Display message on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Your Turn"));
    lcd.setCursor(0, 1);
    lcd.print(F("Attack CPU"));
    // Update both LED matrices
    updatePlayerMatrix();
    updateCPUMatrix();
    return;
  }

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  // Update cursor position
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
        // Red button pressed, attempt to place ship
        if (canPlaceShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal)) {
          placeShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal);
          shipId++;
          playBuzzerTone(1000, 200);  // Confirmation tone

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

          displayUpdated = true;
        } else {
          // Cannot place ship here
          playBuzzerTone(500, 200);  // Error tone
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Cannot Place"));
          lcd.setCursor(0, 1);
          lcd.print(F("Here"));
          delay(500);
          displayUpdated = true;
        }
      }
    }
  }
  lastRedButtonState = redReading;

  // Handle orientation change with joystick button
  handleJoystickButton();

  // If cursor moved or display needs updating
  if (cursorMoved || displayUpdated) {
    // Display current ship placement status on LCD
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

    // Update the player's LED matrix
    updatePlayerMatrix();
  }
}

bool updateCursorPosition() {
  int16_t xValue = analogRead(JOYSTICK_X_PIN);
  int16_t yValue = analogRead(JOYSTICK_Y_PIN);
  unsigned long currentTime = millis();
  bool moved = false;

  if (currentTime - lastJoystickMoveTime > JOYSTICK_MOVE_DELAY) {
    // Left and right movement
    if (xValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      cursorX = (cursorX > 0) ? cursorX - 1 : 0;
      lastJoystickMoveTime = currentTime;
      moved = true;
    } else if (xValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      cursorX = (cursorX < GRID_SIZE - 1) ? cursorX + 1 : GRID_SIZE - 1;
      lastJoystickMoveTime = currentTime;
      moved = true;
    }

    // Up and down movement (Y-axis reversed)
    if (yValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      cursorY = (cursorY > 0) ? cursorY - 1 : 0;
      lastJoystickMoveTime = currentTime;
      moved = true;
    } else if (yValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      cursorY = (cursorY < GRID_SIZE - 1) ? cursorY + 1 : GRID_SIZE - 1;
      lastJoystickMoveTime = currentTime;
      moved = true;
    }
  }

  return moved;
}

void handleJoystickButton() {
  joystickButtonState = digitalRead(JOYSTICK_BUTTON_PIN);

  if (joystickButtonState != lastJoystickButtonState) {
    if (joystickButtonState == LOW) {
      // Button pressed, toggle orientation
      playerShipHorizontal = !playerShipHorizontal;
      playBuzzerTone(800, 100);
    }
    lastJoystickButtonState = joystickButtonState;
  }
}

void playerAttack() {
  bool displayUpdated = false;

  // Update cursor position
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
        // Red button pressed, attempt to attack
        if (getCellState(playerAttackGrid, cursorX, cursorY) == CELL_EMPTY) {
          // Proceed with attack
          uint8_t cellState = getCellState(arduinoGrid, cursorX, cursorY);
          if (cellState == CELL_SHIP) {
            // Hit
            setCellState(playerAttackGrid, cursorX, cursorY, CELL_HIT);
            setCellState(arduinoGrid, cursorX, cursorY, CELL_HIT);
            playBuzzerTone(1500, 500);  // Hit tone

            // Display hit confirmation on LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Hit!"));
            lcd.setCursor(0, 1);
            lcd.print(F("X:"));
            lcd.print(cursorX);
            lcd.print(F(" Y:"));
            lcd.print(cursorY);

            displayUpdated = true;
          } else {
            // Miss
            setCellState(playerAttackGrid, cursorX, cursorY, CELL_MISS);
            setCellState(arduinoGrid, cursorX, cursorY, CELL_MISS);
            playBuzzerTone(500, 200);  // Miss tone

            // Display miss confirmation on LCD
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("Missed"));
            lcd.setCursor(0, 1);
            lcd.print(F("X:"));
            lcd.print(cursorX);
            lcd.print(F(" Y:"));
            lcd.print(cursorY);

            displayUpdated = true;
          }

          // Check if player has won
          uint8_t arduinoShipsRemaining = countRemainingShips(arduinoGrid);
          if (arduinoShipsRemaining == 0) {
            gameState = GAME_OVER;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(F("You Win!"));
            lcd.setCursor(0, 1);
            lcd.print(F("Press Blue Btn"));
            playBuzzerTone(2000, 1000);  // Victory tone
            updateCPUMatrix(); // Update CPU matrix to reflect the win
            return;
          }

          // Switch to Arduino's turn
          gameState = ARDUINO_TURN;
        } else {
          // Already tried this position
          playBuzzerTone(500, 200);  // Error tone
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("Already"));
          lcd.setCursor(0, 1);
          lcd.print(F("Attacked"));
          delay(500);
          displayUpdated = true;
        }
      }
    }
  }
  lastRedButtonState = redReading;

  // If cursor moved or display needs updating
  if (cursorMoved || displayUpdated) {
    // Update the CPU's LED matrix
    updateCPUMatrix();
  }
}

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

    playBuzzerTone(1500, 500);  // Hit tone or confirmation

    if (getCellState(playerGrid, x, y) == CELL_SHIP) {
      // Hit
      setCellState(arduinoAttackGrid, x, y, CELL_HIT);
      setCellState(playerGrid, x, y, CELL_HIT);
      lastHitX = x;
      lastHitY = y;
      arduinoAttackMode = TARGET_MODE;
      targetIndex = 0;
      targetListInitialized = false;

      // Display hit confirmation on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("CPU Hit!"));
      lcd.setCursor(0, 1);
      lcd.print(F("X:"));
      lcd.print(x);
      lcd.print(F(" Y:"));
      lcd.print(y);
    } else {
      // Miss
      setCellState(arduinoAttackGrid, x, y, CELL_MISS);
      setCellState(playerGrid, x, y, CELL_MISS);
      playBuzzerTone(500, 200);  // Miss tone

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

      if (lastHitX > 0) {
        targetList[idx][0] = lastHitX - 1;
        targetList[idx][1] = lastHitY;
        idx++;
      }
      if (lastHitX < GRID_SIZE - 1) {
        targetList[idx][0] = lastHitX + 1;
        targetList[idx][1] = lastHitY;
        idx++;
      }
      if (lastHitY > 0) {
        targetList[idx][0] = lastHitX;
        targetList[idx][1] = lastHitY - 1;
        idx++;
      }
      if (lastHitY < GRID_SIZE - 1) {
        targetList[idx][0] = lastHitX;
        targetList[idx][1] = lastHitY + 1;
        idx++;
      }

      targetListInitialized = true;
    }

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
      if (getCellState(playerGrid, x, y) == CELL_SHIP) {
        // Hit
        setCellState(arduinoAttackGrid, x, y, CELL_HIT);
        setCellState(playerGrid, x, y, CELL_HIT);
        lastHitX = x;
        lastHitY = y;
        arduinoAttackMode = TARGET_MODE;
        targetIndex = 0;
        targetListInitialized = false;

        playBuzzerTone(1500, 500);  // Hit tone

        // Display hit confirmation on LCD
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("CPU Hit!"));
        lcd.setCursor(0, 1);
        lcd.print(F("X:"));
        lcd.print(x);
        lcd.print(F(" Y:"));
        lcd.print(y);
      } else {
        // Miss
        setCellState(arduinoAttackGrid, x, y, CELL_MISS);
        setCellState(playerGrid, x, y, CELL_MISS);
        playBuzzerTone(500, 200);  // Miss tone

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
      // No valid adjacent targets left, switch back to hunt mode
      arduinoAttackMode = HUNT_MODE;
      arduinoAttack(); // Retry attack in hunt mode
      return;
    }
  }

  // Check if Arduino has won
  uint8_t playerShipsRemaining = countRemainingShips(playerGrid);
  if (playerShipsRemaining == 0) {
    gameState = GAME_OVER;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("CPU Wins!"));
    lcd.setCursor(0, 1);
    lcd.print(F("Press Blue Btn"));
    playBuzzerTone(100, 1000);  // Defeat tone
    updatePlayerMatrix(); // Update player's matrix to reflect the loss
    return;
  }

  // Switch back to player's turn
  gameState = PLAYER_TURN;
}

void playBuzzerTone(uint16_t frequency, uint16_t duration) {
  tone(BUZZER_PIN, frequency, duration);
}

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

void updatePlayerMatrix() {
  // Clear the player's LEDs
  fill_solid(ledsPlayer, NUM_LEDS, CRGB::Black);

  // Loop through the player's grid
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int16_t ledIndex = getLEDIndex(x, y);

      if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position during ship placement
        if (gameState == PLACING_SHIPS && x == cursorX && y == cursorY) {
          color = CRGB::White;
        }

        // Cell state
        uint8_t cellState = getCellState(playerGrid, x, y);

        if (cellState == CELL_SHIP) {
          color = CRGB::Green; // Ship part
        } else if (cellState == CELL_HIT) {
          color = CRGB::Red; // Hit ship part
        } else if (cellState == CELL_MISS) {
          color = CRGB::Blue; // Missed attack
        }

        // Ship preview during placement
        if (gameState == PLACING_SHIPS && isShipPreviewPosition(x, y)) {
          color = CRGB::Yellow;  // Ship preview
        }

        ledsPlayer[ledIndex] = color;
      }
    }
  }

  // Show the LEDs
  FastLED.show();
}

void updateCPUMatrix() {
  // Clear the CPU's LEDs
  fill_solid(ledsCPU, NUM_LEDS, CRGB::Black);

  // Loop through the CPU's grid
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int16_t ledIndex = getLEDIndex(x, y);

      if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position during attack phase
        if (gameState == PLAYER_TURN && x == cursorX && y == cursorY) {
          color = CRGB::White;
        }

        // Cell state
        uint8_t attackState = getCellState(playerAttackGrid, x, y);

        if (attackState == CELL_HIT) {
          color = CRGB::Red; // Hit ship part
        } else if (attackState == CELL_MISS) {
          color = CRGB::Blue; // Missed attack
        } else {
          // Hide ships, do not show CELL_SHIP
          color = CRGB::Black;
        }

        ledsCPU[ledIndex] = color;
      }
    }
  }

  // Show the LEDs
  FastLED.show();
}

bool isShipPreviewPosition(uint8_t x, uint8_t y) {
  // Only during ship placement
  if (gameState != PLACING_SHIPS) {
    return false;
  }

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  // Calculate the positions the ship would occupy
  uint8_t shipSize = currentShip.size;

  if (playerShipHorizontal) {
    if (cursorX + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = cursorX + i;
      uint8_t yi = cursorY;
      if (x == xi && y == yi) {
        return true;
      }
    }
  } else {
    if (cursorY + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = cursorX;
      uint8_t yi = cursorY + i;
      if (x == xi && y == yi) {
        return true;
      }
    }
  }

  return false;
}

int16_t getLEDIndex(int16_t x, int16_t y) {
  // Map x and y (0-9) to index (0-99)
  if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
    return -1; // Invalid index
  }

  int16_t index = y * GRID_SIZE + x;
  return index;
}

void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t bitIndex = index * 2; // Each cell uses 2 bits
  uint16_t byteIndex = bitIndex / 8;
  uint8_t bitOffset = bitIndex % 8;
  uint8_t mask = 0b11 << bitOffset;

  grid[byteIndex] = (grid[byteIndex] & ~mask) | ((state & 0b11) << bitOffset);

  // Handle cross-byte boundary
  if (bitOffset > 6) {
    byteIndex++;
    bitOffset = (bitOffset + 2) % 8;
    mask = 0b11 >> (6 - bitOffset);
    grid[byteIndex] = (grid[byteIndex] & ~mask) | ((state & 0b11) >> (8 - bitOffset));
  }
}

uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t bitIndex = index * 2; // Each cell uses 2 bits
  uint16_t byteIndex = bitIndex / 8;
  uint8_t bitOffset = bitIndex % 8;
  uint8_t state = (grid[byteIndex] >> bitOffset) & 0b11;

  // Handle cross-byte boundary
  if (bitOffset > 6) {
    byteIndex++;
    bitOffset = (bitOffset + 2) % 8;
    uint8_t nextBits = grid[byteIndex] & (0b11 >> (6 - bitOffset));
    state |= nextBits << (8 - bitOffset);
  }

  return state;
}
