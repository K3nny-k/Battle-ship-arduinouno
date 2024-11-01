#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdint.h> // For int8_t data type

// Define pins
#define RED_BUTTON_PIN 2    // For placing ships and confirming attacks
#define BLUE_BUTTON_PIN 3   // For starting/resetting the game and toggling orientation
#define BUZZER_PIN 9
#define JOYSTICK_X_PIN A0
#define JOYSTICK_Y_PIN A1

// LCD address and size (adjust the address if needed)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Game constants
#define GRID_SIZE 12
#define NUM_SHIPS 7

// Grid state constants
const int8_t EMPTY = 0;
const int8_t SHIP_HIT = -1;
const uint8_t ATTACK_UNTRIED = 0;
const uint8_t ATTACK_MISS = 1;
const uint8_t ATTACK_HIT = 2;

// Ship definitions
struct Ship {
  const char* name;
  uint8_t size;
};

// Ships array stored in PROGMEM
const Ship ships[NUM_SHIPS] PROGMEM = {
  {"Carrier", 5},
  {"Battleship", 4},
  {"Cruiser", 3},
  {"Submarine", 3},
  {"Destroyer", 2},
  {"Patrol Boat", 2},
  {"Dinghy", 1}
};

// Game grids using int8_t instead of byte
int8_t playerGrid[GRID_SIZE][GRID_SIZE];   // 0 = empty, positive numbers for ship IDs, -1 = hit ship part
int8_t arduinoGrid[GRID_SIZE][GRID_SIZE];  // Same as above

// Attack grids to track hits and misses
uint8_t playerAttackGrid[GRID_SIZE][GRID_SIZE];   // 0 = untried, 1 = miss, 2 = hit
uint8_t arduinoAttackGrid[GRID_SIZE][GRID_SIZE];  // Same as above

// Cursor position
uint8_t cursorX = 0;
uint8_t cursorY = 0;

// Button state variables
byte redButtonState = HIGH;
byte lastRedButtonState = HIGH;
byte blueButtonState = HIGH;
byte lastBlueButtonState = HIGH;

// Blue button long press variables
unsigned long blueButtonPressTime = 0;
bool blueButtonLongPressHandled = false;
bool blueButtonShortPress = false;

// Joystick thresholds
#define JOYSTICK_THRESHOLD 200
#define JOYSTICK_CENTER 512
unsigned long lastJoystickMoveTime = 0;
#define JOYSTICK_MOVE_DELAY 200  // Delay between cursor movements in ms

// Debounce variables for red button
unsigned long lastRedDebounceTime = 0;
unsigned long debounceDelay = 50; // 50 milliseconds debounce delay

// Game states
enum GameState { WAITING_TO_START, PLACING_SHIPS, PLAYER_TURN, ARDUINO_TURN, GAME_OVER };
GameState gameState = WAITING_TO_START;

// Orientation variable for player's ships
boolean playerShipHorizontal = true; // true for horizontal, false for vertical

// Arduino attack state variables
enum AttackMode { HUNT_MODE, TARGET_MODE };
AttackMode arduinoAttackMode = HUNT_MODE;

uint8_t lastHitX;
uint8_t lastHitY;
uint8_t targetIndex;
uint8_t targetList[4][2]; // Possible adjacent targets
bool targetListInitialized = false;

// Ship ID variable moved to global scope
uint8_t shipId = 0; // Moved from static inside playerPlaceShips()

// Function prototypes
void initializeGrids();
void placeArduinoShips();
void playerPlaceShips();
void playerAttack();
void arduinoAttack();
boolean checkWin(int8_t grid[GRID_SIZE][GRID_SIZE]);
boolean updateCursorPosition();
void playBuzzerTone(int frequency, int duration);
boolean canPlaceShip(int8_t grid[GRID_SIZE][GRID_SIZE], uint8_t x, uint8_t y, uint8_t size, boolean horizontal);
void placeShip(int8_t grid[GRID_SIZE][GRID_SIZE], uint8_t x, uint8_t y, uint8_t size, boolean horizontal, uint8_t shipId);
void displayPlayerGrid();
void displayAttackGrid();
void resetGame();
uint8_t countRemainingShips(int8_t grid[GRID_SIZE][GRID_SIZE]);
void displayShipCounts(uint8_t playerShips, uint8_t arduinoShips);

void setup() {
  // Initialize components
  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();

  Serial.begin(9600);

  // Display welcome message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Battleship Game"));
  lcd.setCursor(0, 1);
  lcd.print(F("Press Blue Btn"));

  Serial.println(F("Battleship Game"));
  Serial.println(F("Press the blue button to start."));
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
      if (pressDuration >= 5000) {
        // Long press handled in the loop
      } else {
        // Short press detected
        blueButtonShortPress = true;
      }
      blueButtonPressTime = 0;
    }
    lastBlueButtonState = blueButtonState;
  }

  // Handle long press
  if (blueButtonState == LOW && !blueButtonLongPressHandled) {
    unsigned long pressDuration = millis() - blueButtonPressTime;
    if (pressDuration >= 5000) {
      // Long press detected
      resetGame();
      playBuzzerTone(1000, 200);
      blueButtonLongPressHandled = true;
      blueButtonShortPress = false; // Prevent short press action
    }
  }

  // Handle short press actions based on game state
  if (blueButtonShortPress) {
    blueButtonShortPress = false; // Reset flag after handling

    if (gameState == WAITING_TO_START || gameState == GAME_OVER) {
      // Start or reset the game
      resetGame();
      playBuzzerTone(1000, 200);
    } else if (gameState == PLACING_SHIPS) {
      // Toggle orientation
      playerShipHorizontal = !playerShipHorizontal;
      playBuzzerTone(800, 100);
    } else {
      // In other game states, short press does nothing
    }
  }

  // Handle game states
  switch (gameState) {
    case WAITING_TO_START:
      // Waiting for blue button press, already handled
      break;
    case PLACING_SHIPS:
      playerPlaceShips();
      break;
    case PLAYER_TURN:
      playerAttack();
      break;
    case ARDUINO_TURN:
      arduinoAttack();
      break;
    case GAME_OVER:
      // Waiting for blue button press, already handled
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

  // Display message
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Place your ships"));
  lcd.setCursor(0, 1);
  lcd.print(F("Use Btns & Joy"));

  Serial.println(F("Game reset."));
  Serial.println(F("Player, place your ships."));
}

void initializeGrids() {
  // Initialize all grids to their starting states
  for (uint8_t i = 0; i < GRID_SIZE; i++) {
    for (uint8_t j = 0; j < GRID_SIZE; j++) {
      playerGrid[i][j] = EMPTY;
      arduinoGrid[i][j] = EMPTY;
      playerAttackGrid[i][j] = ATTACK_UNTRIED;
      arduinoAttackGrid[i][j] = ATTACK_UNTRIED;
    }
  }
}

void placeArduinoShips() {
  // Place Arduino ships randomly
  for (uint8_t shipId = 0; shipId < NUM_SHIPS; shipId++) {
    uint8_t shipSize = pgm_read_byte(&(ships[shipId].size));
    boolean placed = false;

    while (!placed) {
      uint8_t x = random(0, GRID_SIZE);
      uint8_t y = random(0, GRID_SIZE);
      boolean horizontal = random(0, 2);  // 0 for vertical, 1 for horizontal

      // Check if ship can be placed
      if (canPlaceShip(arduinoGrid, x, y, shipSize, horizontal)) {
        placeShip(arduinoGrid, x, y, shipSize, horizontal, shipId + 1);
        placed = true;
        // For debugging purposes, you can print Arduino's ship placements if needed
      }
    }
  }
}

boolean canPlaceShip(int8_t grid[GRID_SIZE][GRID_SIZE], uint8_t x, uint8_t y, uint8_t size, boolean horizontal) {
  if (horizontal) {
    if (x + size > GRID_SIZE) return false;

    for (uint8_t i = 0; i < size; i++) {
      uint8_t xi = x + i;
      uint8_t yi = y;

      // Check if current position is empty
      if (grid[xi][yi] != EMPTY) return false;

      // Check surrounding cells
      for (int8_t dx = -1; dx <= 1; dx++) {
        for (int8_t dy = -1; dy <= 1; dy++) {
          int8_t nx = xi + dx;
          int8_t ny = yi + dy;
          if (nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
            if (grid[nx][ny] != EMPTY && !(nx == xi && ny == yi)) {
              return false;
            }
          }
        }
      }
    }
  } else {
    if (y + size > GRID_SIZE) return false;

    for (uint8_t i = 0; i < size; i++) {
      uint8_t xi = x;
      uint8_t yi = y + i;

      // Check if current position is empty
      if (grid[xi][yi] != EMPTY) return false;

      // Check surrounding cells
      for (int8_t dx = -1; dx <= 1; dx++) {
        for (int8_t dy = -1; dy <= 1; dy++) {
          int8_t nx = xi + dx;
          int8_t ny = yi + dy;
          if (nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
            if (grid[nx][ny] != EMPTY && !(nx == xi && ny == yi)) {
              return false;
            }
          }
        }
      }
    }
  }
  return true;
}

void placeShip(int8_t grid[GRID_SIZE][GRID_SIZE], uint8_t x, uint8_t y, uint8_t size, boolean horizontal, uint8_t shipId) {
  if (horizontal) {
    for (uint8_t i = 0; i < size; i++) {
      grid[x + i][y] = shipId; // Assign ship ID
    }
  } else {
    for (uint8_t i = 0; i < size; i++) {
      grid[x][y + i] = shipId; // Assign ship ID
    }
  }
}

void playerPlaceShips() {
  // Use the global variable shipId
  boolean displayUpdated = false;

  // Check if all ships have been placed
  if (shipId >= NUM_SHIPS) {
    gameState = PLAYER_TURN;
    lcd.clear();
    lcd.print(F("Your Turn"));
    Serial.println(F("All ships placed. Your turn to attack."));
    return;
  }

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  // Update cursor position, check if cursor moved
  boolean cursorMoved = updateCursorPosition();

  // Handle red button for placing ships
  byte redReading = digitalRead(RED_BUTTON_PIN);

  if (redReading != lastRedButtonState) {
    lastRedDebounceTime = millis();
  }

  if ((millis() - lastRedDebounceTime) > debounceDelay) {
    if (redReading != redButtonState) {
      redButtonState = redReading;

      if (redButtonState == LOW) {
        // Red button pressed, attempt to place ship
        if (canPlaceShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal)) {
          placeShip(playerGrid, cursorX, cursorY, currentShip.size, playerShipHorizontal, shipId + 1);
          shipId++;
          playBuzzerTone(1000, 200);  // Confirmation tone
          Serial.print(F("Player placed "));
          Serial.print(currentShip.name);
          Serial.print(F(" at ("));
          Serial.print(cursorX);
          Serial.print(F(", "));
          Serial.print(cursorY);
          Serial.print(F(") "));
          Serial.println(playerShipHorizontal ? "Horizontal" : "Vertical");
          displayUpdated = true;
        } else {
          // Cannot place ship here
          lcd.setCursor(0, 1);
          lcd.print(F("Cannot place here"));
          playBuzzerTone(500, 200);  // Error tone
          delay(500);
          displayUpdated = true;
        }
      }
    }
  }
  lastRedButtonState = redReading;

  // If cursor moved or display needs updating, update LCD and serial monitor
  if (cursorMoved || displayUpdated) {
    // Draw current ship position
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Place "));
    lcd.print(currentShip.name);
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(cursorX);
    lcd.print(F(" Y:"));
    lcd.print(cursorY);
    lcd.print(playerShipHorizontal ? " H" : " V"); // Show orientation

    // Display player's grid on Serial Monitor
    displayPlayerGrid();
  }
}

boolean updateCursorPosition() {
  int xValue = analogRead(JOYSTICK_X_PIN);
  int yValue = analogRead(JOYSTICK_Y_PIN);
  unsigned long currentTime = millis();
  boolean moved = false;

  if (currentTime - lastJoystickMoveTime > JOYSTICK_MOVE_DELAY) {
    if (xValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      cursorX = (cursorX > 0) ? cursorX - 1 : 0;
      lastJoystickMoveTime = currentTime;
      moved = true;
    } else if (xValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      cursorX = (cursorX < GRID_SIZE - 1) ? cursorX + 1 : GRID_SIZE - 1;
      lastJoystickMoveTime = currentTime;
      moved = true;
    }

    if (yValue < JOYSTICK_CENTER - JOYSTICK_THRESHOLD) {
      cursorY = (cursorY > 0) ? cursorY - 1 : 0;
      lastJoystickMoveTime = currentTime;
      moved = true;
    } else if (yValue > JOYSTICK_CENTER + JOYSTICK_THRESHOLD) {
      cursorY = (cursorY < GRID_SIZE - 1) ? cursorY + 1 : GRID_SIZE - 1;
      lastJoystickMoveTime = currentTime;
      moved = true;
    }
  }
  return moved;
}

void playerAttack() {
  boolean displayUpdated = false;

  // Update cursor position
  boolean cursorMoved = updateCursorPosition();

  // Read the state of the red button
  byte reading = digitalRead(RED_BUTTON_PIN);

  // Check if the button state has changed
  if (reading != lastRedButtonState) {
    lastRedDebounceTime = millis();
  }

  // Check if the debounce delay has passed
  if ((millis() - lastRedDebounceTime) > debounceDelay) {
    if (reading != redButtonState) {
      redButtonState = reading;

      // Only trigger on button press (when the button is pressed down)
      if (redButtonState == LOW) {
        // Button pressed, attempt to attack
        if (playerAttackGrid[cursorX][cursorY] == ATTACK_UNTRIED) {
          // Proceed with attack
          if (arduinoGrid[cursorX][cursorY] > 0) {
            // Hit
            playerAttackGrid[cursorX][cursorY] = ATTACK_HIT;
            arduinoGrid[cursorX][cursorY] = SHIP_HIT;  // Mark as hit
            lcd.clear();
            lcd.print(F("It's a Hit!"));
            Serial.print(F("Player hit at ("));
            Serial.print(cursorX);
            Serial.print(F(", "));
            Serial.print(cursorY);
            Serial.println(F(")"));
            playBuzzerTone(1500, 500);  // Hit tone
            displayUpdated = true;
          } else {
            // Miss
            playerAttackGrid[cursorX][cursorY] = ATTACK_MISS;
            lcd.clear();
            lcd.print(F("Missed"));
            Serial.print(F("Player missed at ("));
            Serial.print(cursorX);
            Serial.print(F(", "));
            Serial.print(cursorY);
            Serial.println(F(")"));
            playBuzzerTone(500, 200);  // Miss tone
            displayUpdated = true;
          }

          // Count ships remaining
          uint8_t playerShipsRemaining = countRemainingShips(playerGrid);
          uint8_t arduinoShipsRemaining = countRemainingShips(arduinoGrid);

          // Update Serial Monitor
          Serial.print(F("Player Ships Remaining: "));
          Serial.println(playerShipsRemaining);
          Serial.print(F("Arduino Ships Remaining: "));
          Serial.println(arduinoShipsRemaining);

          // Update LCD
          displayShipCounts(playerShipsRemaining, arduinoShipsRemaining);

          // Check if player has won
          if (arduinoShipsRemaining == 0) {
            gameState = GAME_OVER;
            lcd.clear();
            lcd.print(F("You Win!"));
            Serial.println(F("Player wins the game!"));
            playBuzzerTone(2000, 1000);  // Victory tone
            return;
          }

          // Switch to Arduino's turn
          gameState = ARDUINO_TURN;
        } else {
          // Already tried this position
          lcd.setCursor(0, 1);
          lcd.print(F("Already tried"));
          playBuzzerTone(500, 200);  // Error tone
          delay(500);
          displayUpdated = true;
        }
      }
    }
  }
  lastRedButtonState = reading;

  // If cursor moved or display needs updating, update LCD and serial monitor
  if (cursorMoved || displayUpdated) {
    // Draw current cursor position
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Attack at"));
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(cursorX);
    lcd.print(F(" Y:"));
    lcd.print(cursorY);

    // Display attack grid on Serial Monitor
    displayAttackGrid();
  }
}

void arduinoAttack() {
  uint8_t x, y;
  bool validAttack = false;

  if (arduinoAttackMode == HUNT_MODE) {
    // Hunt Mode: Random attack
    do {
      x = random(0, GRID_SIZE);
      y = random(0, GRID_SIZE);
      if (arduinoAttackGrid[x][y] == ATTACK_UNTRIED) {
        validAttack = true;
      }
    } while (!validAttack);

    Serial.print(F("Arduino attacks at ("));
    Serial.print(x);
    Serial.print(F(", "));
    Serial.print(y);
    Serial.println(F(")"));

    if (playerGrid[x][y] > 0) {
      // Hit
      arduinoAttackGrid[x][y] = ATTACK_HIT;
      playerGrid[x][y] = SHIP_HIT;
      lastHitX = x;
      lastHitY = y;
      arduinoAttackMode = TARGET_MODE;
      targetIndex = 0;
      targetListInitialized = false;
      lcd.clear();
      lcd.print(F("Arduino Hit at"));
      lcd.setCursor(0, 1);
      lcd.print(F("X:"));
      lcd.print(x);
      lcd.print(F(" Y:"));
      lcd.print(y);
      playBuzzerTone(1500, 500);
      Serial.println(F("Arduino scored a hit!"));
    } else {
      // Miss
      arduinoAttackGrid[x][y] = ATTACK_MISS;
      lcd.clear();
      lcd.print(F("Arduino Miss at"));
      lcd.setCursor(0, 1);
      lcd.print(F("X:"));
      lcd.print(x);
      lcd.print(F(" Y:"));
      lcd.print(y);
      playBuzzerTone(500, 200);
      Serial.println(F("Arduino missed."));
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

      if (arduinoAttackGrid[x][y] == ATTACK_UNTRIED) {
        validAttack = true;
        break;
      }
    }

    if (validAttack) {
      Serial.print(F("Arduino attacks at ("));
      Serial.print(x);
      Serial.print(F(", "));
      Serial.print(y);
      Serial.println(F(")"));

      if (playerGrid[x][y] > 0) {
        // Hit
        arduinoAttackGrid[x][y] = ATTACK_HIT;
        playerGrid[x][y] = SHIP_HIT;
        lastHitX = x;
        lastHitY = y;
        targetIndex = 0;
        targetListInitialized = false;
        lcd.clear();
        lcd.print(F("Arduino Hit at"));
        lcd.setCursor(0, 1);
        lcd.print(F("X:"));
        lcd.print(x);
        lcd.print(F(" Y:"));
        lcd.print(y);
        playBuzzerTone(1500, 500);
        Serial.println(F("Arduino scored a hit!"));
      } else {
        // Miss
        arduinoAttackGrid[x][y] = ATTACK_MISS;
        lcd.clear();
        lcd.print(F("Arduino Miss at"));
        lcd.setCursor(0, 1);
        lcd.print(F("X:"));
        lcd.print(x);
        lcd.print(F(" Y:"));
        lcd.print(y);
        playBuzzerTone(500, 200);
        Serial.println(F("Arduino missed."));
      }
    } else {
      // No valid adjacent targets left, switch back to hunt mode
      arduinoAttackMode = HUNT_MODE;
      arduinoAttack(); // Retry attack in hunt mode
      return;
    }
  }

  // Count ships remaining
  uint8_t playerShipsRemaining = countRemainingShips(playerGrid);
  uint8_t arduinoShipsRemaining = countRemainingShips(arduinoGrid);

  // Update Serial Monitor
  Serial.print(F("Player Ships Remaining: "));
  Serial.println(playerShipsRemaining);
  Serial.print(F("Arduino Ships Remaining: "));
  Serial.println(arduinoShipsRemaining);

  // Update LCD
  displayShipCounts(playerShipsRemaining, arduinoShipsRemaining);

  // Display updated player grid
  displayPlayerGrid();

  // Check if Arduino has won
  if (playerShipsRemaining == 0) {
    gameState = GAME_OVER;
    lcd.clear();
    lcd.print(F("Arduino Wins!"));
    Serial.println(F("Arduino wins the game!"));
    playBuzzerTone(100, 1000);  // Defeat tone
    return;
  }

  // Switch back to player's turn
  gameState = PLAYER_TURN;
}

void playBuzzerTone(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
}

void displayPlayerGrid() {
  Serial.println(F("Your Grid:"));
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      // Check if this position is where the cursor is
      if (x == cursorX && y == cursorY) {
        Serial.print(F(" ["));
        // Determine what to display inside the brackets
        if (playerGrid[x][y] == SHIP_HIT) {
          Serial.print(F("X"));  // Hit ship part
        } else if (playerGrid[x][y] > 0) {
          Serial.print(F("S"));  // Ship part
        } else {
          if (arduinoAttackGrid[x][y] == ATTACK_MISS) {
            Serial.print(F("M"));  // Miss
          } else {
            Serial.print(F("."));  // Empty
          }
        }
        Serial.print(F("]"));
      } else {
        // Not the cursor position
        if (playerGrid[x][y] == SHIP_HIT) {
          Serial.print(F(" X"));  // Hit ship part
        } else if (playerGrid[x][y] > 0) {
          Serial.print(F(" S"));  // Ship part
        } else {
          if (arduinoAttackGrid[x][y] == ATTACK_MISS) {
            Serial.print(F(" M"));  // Miss
          } else {
            Serial.print(F(" ."));  // Empty
          }
        }
      }
    }
    Serial.println();
  }
  Serial.println();
}

void displayAttackGrid() {
  Serial.println(F("Attack Grid:"));
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      // Check if this position is where the cursor is
      if (x == cursorX && y == cursorY) {
        Serial.print(F(" ["));
        // Determine what to display inside the brackets
        if (playerAttackGrid[x][y] == ATTACK_HIT) {
          Serial.print(F("H"));  // Hit
        } else if (playerAttackGrid[x][y] == ATTACK_MISS) {
          Serial.print(F("M"));  // Miss
        } else {
          Serial.print(F("."));  // Untried
        }
        Serial.print(F("]"));
      } else {
        // Not the cursor position
        if (playerAttackGrid[x][y] == ATTACK_HIT) {
          Serial.print(F(" H"));  // Hit
        } else if (playerAttackGrid[x][y] == ATTACK_MISS) {
          Serial.print(F(" M"));  // Miss
        } else {
          Serial.print(F(" ."));  // Untried
        }
      }
    }
    Serial.println();
  }
  Serial.println();
}

uint8_t countRemainingShips(int8_t grid[GRID_SIZE][GRID_SIZE]) {
  uint8_t shipsFound[NUM_SHIPS] = {0}; // Array to track ships found
  uint8_t shipsRemaining = 0;

  for (uint8_t x = 0; x < GRID_SIZE; x++) {
    for (uint8_t y = 0; y < GRID_SIZE; y++) {
      if (grid[x][y] > 0) {
        // A ship part is still present
        shipsFound[grid[x][y] - 1] = 1;
      }
    }
  }

  // Count the number of ships remaining
  for (uint8_t i = 0; i < NUM_SHIPS; i++) {
    if (shipsFound[i] == 1) {
      shipsRemaining++;
    }
  }

  return shipsRemaining;
}

void displayShipCounts(uint8_t playerShips, uint8_t arduinoShips) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("You:"));
  lcd.print(playerShips);
  lcd.print(F(" CPU:"));
  lcd.print(arduinoShips);
}
