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
const int8_t SHIP_PRESENT = 1;
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

// Joystick thresholds
#define JOYSTICK_THRESHOLD 200
#define JOYSTICK_CENTER 512
unsigned long lastJoystickMoveTime = 0;
#define JOYSTICK_MOVE_DELAY 200  // Delay between cursor movements in ms

// Debounce variables
unsigned long lastRedDebounceTime = 0;
unsigned long lastBlueDebounceTime = 0;
unsigned long debounceDelay = 50; // 50 milliseconds debounce delay

// Game states
enum GameState { WAITING_TO_START, PLACING_SHIPS, PLAYER_TURN, ARDUINO_TURN, GAME_OVER };
GameState gameState = WAITING_TO_START;

// Orientation variable for player's ships
boolean playerShipHorizontal = true; // true for horizontal, false for vertical

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
  // Handle blue button for starting/resetting the game
  blueButtonState = digitalRead(BLUE_BUTTON_PIN);
  if (gameState == WAITING_TO_START || gameState == GAME_OVER) {
    if (blueButtonState != lastBlueButtonState) {
      if (blueButtonState == LOW) {
        // Blue button pressed
        resetGame();
        playBuzzerTone(1000, 200);  // Confirmation tone
      }
      delay(50); // Debounce delay
    }
    lastBlueButtonState = blueButtonState;
  }

  switch (gameState) {
    case WAITING_TO_START:
      // Waiting for the player to press the blue button
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
      // Game over logic
      lcd.setCursor(0, 1);
      lcd.print(F("Press Blue Btn"));
      // Waiting for reset
      break;
  }

  // Add a small delay to prevent flooding
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
  // Initialize all grids to zero
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
      if (grid[x + i][y] != EMPTY) return false;
    }
  } else {
    if (y + size > GRID_SIZE) return false;
    for (uint8_t i = 0; i < size; i++) {
      if (grid[x][y + i] != EMPTY) return false;
    }
  }
  return true;
}

void placeShip(int8_t grid[GRID_SIZE][GRID_SIZE], uint8_t x, uint8_t y, uint8_t size, boolean horizontal, uint8_t shipId) {
  if (horizontal) {
    for (uint8_t i = 0; i < size; i++) {
      grid[x + i][y] = SHIP_PRESENT;
    }
  } else {
    for (uint8_t i = 0; i < size; i++) {
      grid[x][y + i] = SHIP_PRESENT;
    }
  }
}

void playerPlaceShips() {
  static uint8_t shipId = 0;
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

  // Handle blue button for toggling orientation
  byte blueReading = digitalRead(BLUE_BUTTON_PIN);

  if (blueReading != lastBlueButtonState) {
    lastBlueDebounceTime = millis();
  }

  if ((millis() - lastBlueDebounceTime) > debounceDelay) {
    if (blueReading != blueButtonState) {
      blueButtonState = blueReading;

      if (blueButtonState == LOW) {
        // Blue button pressed, toggle orientation
        playerShipHorizontal = !playerShipHorizontal;
        playBuzzerTone(800, 100);  // Toggle tone
        displayUpdated = true;
      }
    }
  }
  lastBlueButtonState = blueReading;

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

// ... Rest of the code remains the same ...

// Ensure to include all other functions as provided in the previous code.
// The modifications are mainly in the `playerPlaceShips()` function and the addition of the orientation variable.

void playerAttack() {
  boolean displayUpdated = false;

  // Update cursor position
  boolean cursorMoved = updateCursorPosition();

  // Read the state of the red button
  byte reading = digitalRead(RED_BUTTON_PIN);

  // Check if the button state has changed
  if (reading != lastRedButtonState) {
    lastDebounceTime = millis();
  }

  // Check if the debounce delay has passed
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // If the button state has changed
    if (reading != redButtonState) {
      redButtonState = reading;

      // Only trigger on button press (when the button is pressed down)
      if (redButtonState == LOW) {
        // Button pressed, attempt to attack
        if (playerAttackGrid[cursorX][cursorY] == ATTACK_UNTRIED) {
          // Proceed with attack
          if (arduinoGrid[cursorX][cursorY] == SHIP_PRESENT) {
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

          // Check if player has won
          if (checkWin(arduinoGrid)) {
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

  // Save the reading for the next loop iteration
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
  // Simple random attack logic
  uint8_t x, y;
  boolean validAttack = false;

  // Check if there are any untried positions left
  boolean positionsLeft = false;
  for (uint8_t i = 0; i < GRID_SIZE; i++) {
    for (uint8_t j = 0; j < GRID_SIZE; j++) {
      if (arduinoAttackGrid[i][j] == ATTACK_UNTRIED) {
        positionsLeft = true;
        break;
      }
    }
    if (positionsLeft) break;
  }

  if (!positionsLeft) {
    // No positions left to attack
    gameState = GAME_OVER;
    lcd.clear();
    lcd.print(F("Game Over"));
    Serial.println(F("Game over: No positions left to attack."));
    return;
  }

  // Select a random untried position
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

  if (playerGrid[x][y] == SHIP_PRESENT) {
    // Hit
    arduinoAttackGrid[x][y] = ATTACK_HIT;
    playerGrid[x][y] = SHIP_HIT;  // Mark as hit
    lcd.clear();
    lcd.print(F("Arduino Hit at"));
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(x);
    lcd.print(F(" Y:"));
    lcd.print(y);
    playBuzzerTone(1500, 500);  // Hit tone
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
    playBuzzerTone(500, 200);  // Miss tone
    Serial.println(F("Arduino missed."));
  }

  // Display updated player grid
  displayPlayerGrid();

  // Check if Arduino has won
  if (checkWin(playerGrid)) {
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

boolean checkWin(int8_t grid[GRID_SIZE][GRID_SIZE]) {
  // Check if all ships are sunk (SHIP_HIT indicates hit ship part)
  for (uint8_t i = 0; i < GRID_SIZE; i++) {
    for (uint8_t j = 0; j < GRID_SIZE; j++) {
      if (grid[i][j] == SHIP_PRESENT) {
        return false;  // At least one ship part is still afloat
      }
    }
  }
  return true;  // All ships are sunk
}

void playBuzzerTone(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
}

void displayPlayerGrid() {
  Serial.println(F("Your Grid:"));
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      if (playerGrid[x][y] == SHIP_HIT) {
        Serial.print(F(" X"));  // Hit ship part
      } else if (playerGrid[x][y] == SHIP_PRESENT) {
        Serial.print(F(" S"));  // Ship part
      } else {
        if (arduinoAttackGrid[x][y] == ATTACK_MISS) {
          Serial.print(F(" M"));  // Miss
        } else {
          Serial.print(F(" ."));  // Empty
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
      if (playerAttackGrid[x][y] == ATTACK_HIT) {
        Serial.print(F(" H"));  // Hit
      } else if (playerAttackGrid[x][y] == ATTACK_MISS) {
        Serial.print(F(" M"));  // Miss
      } else {
        Serial.print(F(" ."));  // Untried
      }
    }
    Serial.println();
  }
  Serial.println();
}
