// Battleship Game Arduino Code with Single 16x16 LED Matrix Representing Two 8x8 Grids

#include <FastLED.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --------------------- Definitions ---------------------

// Define pins
#define BUZZER_PIN 8
#define JOYSTICK_X_PIN A3
#define JOYSTICK_Y_PIN A1
#define JOYSTICK_BUTTON_PIN 2  // Joystick button pin (for orientation change)
#define RED_BUTTON_PIN 12      // For placing ships and confirming attacks
#define BLUE_BUTTON_PIN 13     // For starting/resetting the game
#define CPU_SIGNAL_PIN 7       // Signal pin to CPU Arduino

// LED Matrix definitions
#define NUM_LEDS_TOTAL 256 // 16x16 LED matrix
#define DATA_PIN 3

CRGB leds[NUM_LEDS_TOTAL]; // Single LED array for 16x16 matrix

// Define the size of the game grid
#define GRID_SIZE 8 // Each grid is 8x8

// Game constants
#define NUM_SHIPS 4  // Reduced number of ships

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

const Ship ships[NUM_SHIPS] PROGMEM = {
  {"Carrier", 5},
  {"Battleship", 4},
  {"Cruiser", 3},
  {"Destroyer", 2}
};

// Grids represented as bitfields (arrays of bytes)
uint8_t playerGrid[(GRID_SIZE * GRID_SIZE + 3) / 4 + 1];       // 17 bytes
uint8_t arduinoGrid[(GRID_SIZE * GRID_SIZE + 3) / 4 + 1];      // 17 bytes
uint8_t playerAttackGrid[(GRID_SIZE * GRID_SIZE + 3) / 4 + 1]; // 17 bytes
uint8_t arduinoAttackGrid[(GRID_SIZE * GRID_SIZE + 3) / 4 + 1];// 17 bytes

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
unsigned long lastBlueDebounceTime = 0;
const unsigned long blueDebounceDelay = 50; // 50 ms debounce

// Debounce variables for red button
unsigned long lastRedDebounceTime = 0;
const unsigned long redDebounceDelay = 50; // 50 milliseconds debounce delay

// Joystick thresholds
#define JOYSTICK_THRESHOLD 200
#define JOYSTICK_CENTER 512
unsigned long lastJoystickMoveTime = 0;
const unsigned long joystickMoveDelay = 200;  // Delay between moves in ms

// Joystick button variables for orientation change
byte joystickButtonState = HIGH;
byte lastJoystickButtonState = HIGH;
unsigned long joystickButtonPressTime = 0;
bool joystickButtonLongPressHandled = false;
bool joystickButtonShortPress = false;
unsigned long lastJoystickButtonTime = 0;
const unsigned long joystickButtonDebounceDelay = 50;
unsigned long lastJoystickButtonReleaseTime = 0;
const unsigned long joystickDoubleClickThreshold = 500; // 500 ms
int joystickButtonClickCount = 0;

// Game states
enum GameState { WAITING_TO_START, PLACING_SHIPS,
                 PLAYER_TURN, ARDUINO_TURN, GAME_OVER };
GameState gameState = WAITING_TO_START;

// Orientation variable for player's ships
bool playerShipHorizontal = true; // true = horizontal

// Arduino attack state variables
enum AttackMode { HUNT_MODE, TARGET_MODE };
AttackMode arduinoAttackMode = HUNT_MODE;

uint8_t lastHitX;
uint8_t lastHitY;
uint8_t targetIndex;
uint8_t targetList[4][2]; // Possible adjacent targets
bool targetListInitialized = false;

// Ship ID variable
uint8_t shipId = 0;

// LCD Initialization
LiquidCrystal_I2C lcd(0x27, 16, 2); // Initialize LCD with I2C address 0x27

// --------------------- Function Prototypes ---------------------
void initializeGrids();
void placeArduinoShips();
void playerPlaceShips();
void playerAttack();
void arduinoAttack();
bool updateCursorPosition();
void playBuzzerTone(int frequency, int duration);
bool canPlaceShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal);
void placeShip(uint8_t* grid, uint8_t x, uint8_t y, uint8_t size, bool horizontal);
void resetGame();
uint8_t countRemainingShips(uint8_t* grid);
void displayShipCounts(uint8_t playerShips, uint8_t arduinoShips);
void updateLEDMatrix();
bool isShipPreviewPosition(uint8_t x, uint8_t y);
int getLEDIndex(int x, int y, bool isPlacement);
void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state);
uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y);
void handleJoystickButton();
void updatePlacementLEDMatrix();
void updateAttackLEDMatrix();

// --------------------- Setup Function ---------------------
void setup() {
  // Initialize components
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BLUE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CPU_SIGNAL_PIN, OUTPUT);
  digitalWrite(CPU_SIGNAL_PIN, LOW); // Ensure it's LOW at start

  // Initialize LEDs for 16x16 Matrix
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS_TOTAL);
  FastLED.setBrightness(20); // Adjust brightness as needed
  FastLED.clear();
  FastLED.show();

  // Initial Refresh: Light up both grids with distinct colors
  // Placement Grid (Top-Left 8x8) - Green
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, true);
      if (ledIndex != -1) {
        leds[ledIndex] = CRGB::Green;
      }
    }
  }

  // Attack Grid (Top-Right 8x8) - Blue
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, false);
      if (ledIndex != -1) {
        leds[ledIndex] = CRGB::Blue;
      }
    }
  }

  FastLED.show();
  delay(2000); // Hold the test pattern for 2 seconds

  // Clear both grids
  FastLED.clear();
  FastLED.show();

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Initialize Serial Communication
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

// --------------------- Main Loop ---------------------
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
      if (pressDuration >= 2000) {
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
      break;
    case PLAYER_TURN:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      playerAttack();
      break;
    case ARDUINO_TURN:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      arduinoAttack();
      break;
    case GAME_OVER:
      digitalWrite(CPU_SIGNAL_PIN, LOW);
      // Waiting for blue button press
      break;
  }

  // Update both LED matrices
  updateLEDMatrix();

  // Small delay to prevent flooding
  delay(50);
}

// --------------------- Function Implementations ---------------------

// Initialize all grids to empty
void initializeGrids() {
  memset(playerGrid, 0, sizeof(playerGrid));
  memset(arduinoGrid, 0, sizeof(arduinoGrid));
  memset(playerAttackGrid, 0, sizeof(playerAttackGrid));
  memset(arduinoAttackGrid, 0, sizeof(arduinoAttackGrid));
}

// Randomly place Arduino ships on its grid
void placeArduinoShips() {
  // Seed random number generator
  randomSeed(analogRead(0));

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

// Handle ship placement by the player
void playerPlaceShips() {
  bool displayUpdated = false;

  // Check if all ships have been placed
  if (shipId >= NUM_SHIPS) {
    gameState = PLAYER_TURN;
    lcd.clear();
    lcd.print(F("Your Turn"));
    Serial.println(F("All ships placed. Your turn to attack."));
    updateLEDMatrix();
    return;
  }

  // Retrieve current ship data from PROGMEM
  Ship currentShip;
  memcpy_P(&currentShip, &ships[shipId], sizeof(Ship));

  // Update cursor position
  bool cursorMoved = updateCursorPosition();

  // Handle red button for placing ships
  byte redReading = digitalRead(RED_BUTTON_PIN);

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

  // Handle orientation change with joystick button
  handleJoystickButton();

  // If cursor moved or display needs updating
  if (cursorMoved || displayUpdated) {
    // Draw current ship position on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Place "));
    lcd.print(currentShip.name);
    lcd.setCursor(0, 1);
    lcd.print(F("X:"));
    lcd.print(cursorX);
    lcd.print(F(" Y:"));
    lcd.print(cursorY);
    lcd.print(playerShipHorizontal ? " H" : " V"); // Orientation

    // Update the LED matrices
    updateLEDMatrix();
  }
}

// Handle player's attack phase
void playerAttack() {
  bool displayUpdated = false;

  // Update cursor position
  bool cursorMoved = updateCursorPosition();

  // Handle red button for confirming attacks
  byte redReading = digitalRead(RED_BUTTON_PIN);

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
            setCellState(playerAttackGrid, cursorX, cursorY, CELL_MISS);
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
          uint8_t arduinoShipsRemaining = countRemainingShips(arduinoGrid);
          if (arduinoShipsRemaining == 0) {
            gameState = GAME_OVER;
            lcd.clear();
            lcd.print(F("You Win!"));
            Serial.println(F("Player wins the game!"));
            playBuzzerTone(2000, 1000);  // Victory tone
            updateLEDMatrix(); // Update LEDs to reflect the win
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
  lastRedButtonState = redReading;

  // If cursor moved or display needs updating
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

    // Update the LED matrices
    updateLEDMatrix();
  }
}

// Handle Arduino's attack phase
void arduinoAttack() {
  uint8_t x, y;
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

    Serial.print(F("Arduino attacks at ("));
    Serial.print(x);
    Serial.print(F(", "));
    Serial.print(y);
    Serial.println(F(")"));

    if (getCellState(playerGrid, x, y) == CELL_SHIP) {
      // Hit
      setCellState(arduinoAttackGrid, x, y, CELL_HIT);
      setCellState(playerGrid, x, y, CELL_HIT);
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
      setCellState(arduinoAttackGrid, x, y, CELL_MISS);
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

      if (getCellState(arduinoAttackGrid, x, y) == CELL_EMPTY) {
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

      if (getCellState(playerGrid, x, y) == CELL_SHIP) {
        // Hit
        setCellState(arduinoAttackGrid, x, y, CELL_HIT);
        setCellState(playerGrid, x, y, CELL_HIT);
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
        setCellState(arduinoAttackGrid, x, y, CELL_MISS);
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

  // Check if Arduino has won
  uint8_t playerShipsRemaining = countRemainingShips(playerGrid);
  if (playerShipsRemaining == 0) {
    gameState = GAME_OVER;
    lcd.clear();
    lcd.print(F("Arduino Wins!"));
    Serial.println(F("Arduino wins the game!"));
    playBuzzerTone(100, 1000);  // Defeat tone
    updateLEDMatrix(); // Update LEDs to reflect the loss
    return;
  }

  // Switch back to player's turn
  gameState = PLAYER_TURN;
}

// Play buzzer tone with given frequency and duration
void playBuzzerTone(int frequency, int duration) {
  tone(BUZZER_PIN, frequency, duration);
}

// Count remaining ships on a grid
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

// Display remaining ship counts on LCD
void displayShipCounts(uint8_t playerShips, uint8_t arduinoShips) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("You:"));
  lcd.print(playerShips);
  lcd.print(F(" CPU:"));
  lcd.print(arduinoShips);
}

// Update both Placement and Attack LED matrices
void updateLEDMatrix() {
  updatePlacementLEDMatrix(); // Update Player's Grid LED Matrix
  updateAttackLEDMatrix();    // Update Attack Grid LED Matrix
  FastLED.show();             // Update the LED matrix
}

// Check if the current ship placement position is valid for preview
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
    if (x + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = x + i;
      uint8_t yi = y;
      if (x == xi && y == yi) {
        return true;
      }
    }
  } else {
    if (y + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = x;
      uint8_t yi = y + i;
      if (x == xi && y == yi) {
        return true;
      }
    }
  }

  return false;
}

// Map 8x8 grid coordinates to 16x16 LED matrix indices
int getLEDIndex(int x, int y, bool isPlacement) {
  // Define offsets for each grid within the 16x16 matrix
  // Placement Grid: Top-Left (0,0)
  // Attack Grid: Top-Right (8,0)
  int offsetX = isPlacement ? 0 : 8;
  int offsetY = 0; // Both grids start at y=0; adjust if needed

  int physicalX = x + offsetX;
  int physicalY = y + offsetY;

  // Ensure coordinates are within the 16x16 matrix
  if (physicalX >= 16 || physicalY >= 16 || physicalX < 0 || physicalY < 0) {
    return -1; // Invalid index
  }

  // Calculate LED index assuming row-major order
  int ledIndex = physicalY * 16 + physicalX;

  return ledIndex;
}

// Set the state of a cell in the grid
void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t byteIndex = index / 4;
  uint8_t bitOffset = (index % 4) * 2;
  grid[byteIndex] &= ~(0b11 << bitOffset); // Clear bits
  grid[byteIndex] |= (state & 0b11) << bitOffset; // Set bits
}

// Get the state of a cell in the grid
uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t byteIndex = index / 4;
  uint8_t bitOffset = (index % 4) * 2;
  return (grid[byteIndex] >> bitOffset) & 0b11;
}

// Reset the game to initial state
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
  joystickButtonPressTime = 0;
  joystickButtonLongPressHandled = false;
  joystickButtonShortPress = false;
  joystickButtonClickCount = 0;

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

  // Update the LED matrices
  updateLEDMatrix();
}

// Update Placement LED Matrix (Top-Left 8x8)
void updatePlacementLEDMatrix() {
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, true);

      if (ledIndex != -1) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position
        if (x == cursorX && y == cursorY && gameState == PLACING_SHIPS) {
          color = CRGB::White;
        }

        // Cell state
        uint8_t cellState = getCellState(playerGrid, x, y);

        if (cellState == CELL_SHIP) {
          color = CRGB::Green; // Ship part
        } else if (cellState == CELL_HIT) {
          color = CRGB::Red; // Hit ship part
        }

        // Ship preview during placement
        if (gameState == PLACING_SHIPS &&
            isShipPreviewPosition(x, y)) {
          color = CRGB::Yellow;  // Ship preview
        }

        // Hits by Arduino
        uint8_t arduinoAttackState = getCellState(arduinoAttackGrid, x, y);
        if (arduinoAttackState == CELL_HIT) {
          color = CRGB::Red; // Hit by Arduino
        } else if (arduinoAttackState == CELL_MISS) {
          color = CRGB::Blue; // Missed attack by Arduino
        }

        leds[ledIndex] = color;
      }
    }
  }
}

// Update Attack LED Matrix (Top-Right 8x8)
void updateAttackLEDMatrix() {
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, false);

      if (ledIndex != -1) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position
        if (x == cursorX && y == cursorY && gameState == PLAYER_TURN) {
          color = CRGB::White;
        }

        // Attack grid state
        uint8_t attackState = getCellState(playerAttackGrid, x, y);

        if (attackState == CELL_HIT) {
          color = CRGB::Red; // Hit
        } else if (attackState == CELL_MISS) {
          color = CRGB::Blue; // Miss
        }

        leds[ledIndex] = color;
      }
    }
  }
}

// Update both Placement and Attack LED matrices
void updateLEDMatrix() {
  updatePlacementLEDMatrix(); // Update Player's Grid LED Matrix
  updateAttackLEDMatrix();    // Update Attack Grid LED Matrix
  FastLED.show();             // Update the LED matrix
}

// Check if the current ship placement position is valid for preview
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
    if (x + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = x + i;
      uint8_t yi = y;
      if (xi == cursorX && yi == cursorY) {
        return true;
      }
    }
  } else {
    if (y + shipSize > GRID_SIZE) {
      // Ship would go out of bounds
      return false;
    }
    for (uint8_t i = 0; i < shipSize; i++) {
      uint8_t xi = x;
      uint8_t yi = y + i;
      if (xi == cursorX && yi == cursorY) {
        return true;
      }
    }
  }

  return false;
}

// Map 8x8 grid coordinates to 16x16 LED matrix indices
int getLEDIndex(int x, int y, bool isPlacement) {
  // Define offsets for each grid within the 16x16 matrix
  // Placement Grid: Top-Left (0,0)
  // Attack Grid: Top-Right (8,0)
  int offsetX = isPlacement ? 0 : 8;
  int offsetY = 0; // Both grids start at y=0; adjust if needed

  int physicalX = x + offsetX;
  int physicalY = y + offsetY;

  // Ensure coordinates are within the 16x16 matrix
  if (physicalX >= 16 || physicalY >= 16 || physicalX < 0 || physicalY < 0) {
    return -1; // Invalid index
  }

  // Calculate LED index assuming row-major order
  int ledIndex = physicalY * 16 + physicalX;

  return ledIndex;
}

// Set the state of a cell in the grid
void setCellState(uint8_t* grid, uint8_t x, uint8_t y, uint8_t state) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t byteIndex = index / 4;
  uint8_t bitOffset = (index % 4) * 2;
  grid[byteIndex] &= ~(0b11 << bitOffset); // Clear bits
  grid[byteIndex] |= (state & 0b11) << bitOffset; // Set bits
}

// Get the state of a cell in the grid
uint8_t getCellState(uint8_t* grid, uint8_t x, uint8_t y) {
  uint16_t index = y * GRID_SIZE + x;
  uint16_t byteIndex = index / 4;
  uint8_t bitOffset = (index % 4) * 2;
  return (grid[byteIndex] >> bitOffset) & 0b11;
}

// Reset the game to initial state
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
  joystickButtonPressTime = 0;
  joystickButtonLongPressHandled = false;
  joystickButtonShortPress = false;
  joystickButtonClickCount = 0;

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

  // Update the LED matrices
  updateLEDMatrix();
}

// Update Placement LED Matrix (Top-Left 8x8)
void updatePlacementLEDMatrix() {
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, true);

      if (ledIndex != -1) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position
        if (x == cursorX && y == cursorY && gameState == PLACING_SHIPS) {
          color = CRGB::White;
        }

        // Cell state
        uint8_t cellState = getCellState(playerGrid, x, y);

        if (cellState == CELL_SHIP) {
          color = CRGB::Green; // Ship part
        } else if (cellState == CELL_HIT) {
          color = CRGB::Red; // Hit ship part
        }

        // Ship preview during placement
        if (gameState == PLACING_SHIPS &&
            isShipPreviewPosition(x, y)) {
          color = CRGB::Yellow;  // Ship preview
        }

        // Hits by Arduino
        uint8_t arduinoAttackState = getCellState(arduinoAttackGrid, x, y);
        if (arduinoAttackState == CELL_HIT) {
          color = CRGB::Red; // Hit by Arduino
        } else if (arduinoAttackState == CELL_MISS) {
          color = CRGB::Blue; // Missed attack by Arduino
        }

        leds[ledIndex] = color;
      }
    }
  }
}

// Update Attack LED Matrix (Top-Right 8x8)
void updateAttackLEDMatrix() {
  for (uint8_t y = 0; y < GRID_SIZE; y++) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
      int ledIndex = getLEDIndex(x, y, false);

      if (ledIndex != -1) {
        // Determine the color for this cell
        CRGB color = CRGB::Black; // Default color

        // Cursor position
        if (x == cursorX && y == cursorY && gameState == PLAYER_TURN) {
          color = CRGB::White;
        }

        // Attack grid state
        uint8_t attackState = getCellState(playerAttackGrid, x, y);

        if (attackState == CELL_HIT) {
          color = CRGB::Red; // Hit
        } else if (attackState == CELL_MISS) {
          color = CRGB::Blue; // Miss
        }

        leds[ledIndex] = color;
      }
    }
  }
}

// Handle joystick button for orientation change (single click)
void handleJoystickButton() {
  joystickButtonState = digitalRead(JOYSTICK_BUTTON_PIN);
  if (joystickButtonState != lastJoystickButtonState) {
    if (joystickButtonState == LOW) {
      // Button pressed
      unsigned long currentTime = millis();
      if (currentTime - lastJoystickButtonReleaseTime < joystickDoubleClickThreshold) {
        joystickButtonClickCount++;
      } else {
        joystickButtonClickCount = 1;
      }
      lastJoystickButtonReleaseTime = currentTime;

      if (joystickButtonClickCount == 2) {
        // Double-click detected, toggle orientation
        playerShipHorizontal = !playerShipHorizontal;
        playBuzzerTone(800, 100);
        joystickButtonClickCount = 0;
      }
    }
    lastJoystickButtonState = joystickButtonState;
  }
}

// Update both Placement and Attack LED matrices
void updateLEDMatrix() {
  updatePlacementLEDMatrix(); // Update Player's Grid LED Matrix
  updateAttackLEDMatrix();    // Update Attack Grid LED Matrix
  FastLED.show();             // Update the LED matrix
}

