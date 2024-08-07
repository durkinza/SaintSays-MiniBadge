// #define USEI2C true
#define USEEEPROM true

#ifdef PINMAPPING_CCW
#error "Sketch was written for clockwise pin mapping!"
#endif

#ifdef USEEEPROM
#include <EEPROM.h>
#endif

#ifdef USEI2C
#include <Wire.h>
#endif

// Pin definitions
#define BUTTONCOUNT 4
#define LEDCOUNT 4
// const uint8_t ledPins[LEDCOUNT] = {10, 9, 8, 7}; // LED pins
// const uint8_t buttonPins[BUTTONCOUNT] = {1, 2, 3, 4}; // Button pins
const uint8_t ledPins[LEDCOUNT] = {10, 9, 8, 7}; // LED pins
const uint8_t buttonPins[BUTTONCOUNT] = {0, 1, 2, 3}; // Button pins
const uint8_t clockPin = 0; // Clock Sensing pin.

// Variables for menu settings
#define SETTINGSCOUNT 4 // Settings count should always be <= button and LED counts, and should never exceed 8.
bool settings[SETTINGSCOUNT] = {true, true, true, true}; // {showScoreWhileAsleep, FastMode, Extra, }
bool buttonsPressed[BUTTONCOUNT] = {false, false, false, false}; // Buttons Pressed State.

// Variables for transitions and debounce
const unsigned int loopDelay = 200; // Give users time to press buttons between loops
const unsigned long resetholdTime = 15000; // 15 seconds in milliseconds
const unsigned long gameTimeout = 60000;  // Wait 1 minute for a button to be pressed.

#if defined(USEI2C)
/*******************************
************ I2C ***************
********************************/
const uint8_t I2CDeviceAddress = 0x42; // I2C address for this device

#endif

#ifdef USEEEPROM
/*******************************
********** EEPROM **************
********************************/
// EEPROM address for high score
const uint8_t EEPROMhighScoreAddress = 0;
const uint8_t EEPROMsettingsAddress = 8;
#endif
// Track highest score
uint8_t highScore = 1;

// Variables for game state
uint8_t pattern[24]; // Max pattern length (Should this be longer than 16?)
uint8_t patternLength = 0; // Current pattern Index (a.k.a current score)
int patternPulseSlow = 500; // How long to show each pattern step for.
int patternPulseQuick = 100; // How long to show each pattern step for.

enum Modes {SLEEPMODE, SETTINGSMODE, GAMEMODE, LIGHTSHOWMODE}; 
Modes mode = SLEEPMODE;

void setup() {
  // Initialize LEDs as outputs
  for (uint8_t i = 0; i < LEDCOUNT; i++) {
    pinMode(ledPins[i], OUTPUT);
  }
  for (uint8_t i = 0; i < LEDCOUNT; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  // Initialize buttons as inputs with pull-up resistors
  for (uint8_t i = 0; i < BUTTONCOUNT; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }


#ifdef USEEEPROM
  // Load the high score and settings from EEPROM.
  readHighScoreFromEEPROM();
  readSettingsFromEEPROM();
#else
  // Set default highscore and settings.
  highScore = 1;
  // memcpy(settings, defaultsettings, SETTINGSCOUNT);
#endif

#ifdef USEI2C
  // Share score on I2C bus
  Wire.begin(I2CDeviceAddress); // Join I2C bus with the given address
  Wire.onRequest(requestI2CReadEvent); // Register event handler for I2C reading
  Wire.onReceive(requestI2CWriteEvent); // Register event handler for I2C writing
#endif

  // Show Startup Sequence
  showStartup();

  // Set initial state
  mode = SLEEPMODE;
}

void loop() {
  Modes starting_mode = mode;
  checkButtonsPressed();
  if(mode == SLEEPMODE){
    handleSleepMode();
  }else if(mode == SETTINGSMODE){
    handleSettingsMode();
  }else if(mode == LIGHTSHOWMODE){
    handleLightShowMode();
  }else if (mode == GAMEMODE) {
    handleGameMode();
  }
  delay(loopDelay);
  // If we are going to sleep mode from another mode, play a sweep.
  if((mode == SLEEPMODE) & (starting_mode != mode)){
    showSweep2();
  }
}


/*******************************
*********** HANDLERS ***********
*******************************/
void handleSleepMode(){
  //Check if a reset is happening
  if(allButtonsPressed()){
    handleResetTimer();
    return;
  }else if(outsideButtonsPressed()){
    mode = SETTINGSMODE;
    return;
  }else if(insideButtonsPressed()){
    mode = LIGHTSHOWMODE;
    return;
  }else if(anyButtonsPressed()){
    mode = GAMEMODE;
    return;
  }else{
    // Check if we should display score in sleep mode;
    if(settings[0]){
      displayHighScore();
    }else{
      // Turn off LEDs if in sleep mode
      for (uint8_t i = 0; i < LEDCOUNT; i++) {
        digitalWrite(ledPins[i], LOW);
      }
    }
  }
  delay(loopDelay);
}

void handleSettingsMode(){
  showSweep(); // Give user time to release side buttons.
  while(true){
    displaySettings();
    checkButtonsPressed();
    if(outsideButtonsPressed()){
      break;
    }else{
      for(uint8_t i = 0; i < SETTINGSCOUNT; i++){
        if(buttonsPressed[i]){
          settings[i] = !settings[i];
        }
      }
    }
    delay(loopDelay);
  }
  #ifdef USEEEPROM
  saveSettingsToEEPROM();
  #endif
  mode = SLEEPMODE;
  delay(loopDelay);
}

void handleGameMode(){
  // Clear last Game
  memset(pattern, 0, sizeof(pattern));
  patternLength = 0;
  randomSeed(analogRead(0));

  // Turn off all LEDs
  turnOffLights();

  while(true){
      pattern[patternLength] = random(0, 4); // from 1 to 4;
      flashPattern();
      // Have user input all parts of pattern.
      for(uint8_t patternIndex=0; patternIndex < patternLength; patternIndex++){
          // loop forever while waiting for user to press a button
          int lastButtonTime = millis();
          while(true){
              checkButtonsPressed();
              if(outsideButtonsPressed()){
                  // If both outside buttons are pressed, leave game.
                  mode = SLEEPMODE;
                  return;
              }
              if(millis() - lastButtonTime > gameTimeout){
                  // If no buttons have been pressed, return to Sleep.
                  mode = SLEEPMODE;
                  return;
              }
              if(anyButtonsPressed()){
                  // Flash LED for pressed buttons
                  for(uint8_t i=0; i < BUTTONCOUNT; i++){
                      if(buttonsPressed[i]){
                          digitalWrite(ledPins[i], HIGH);
                      }
                  }
                  delay(200); // let the user see the light they pressed.
                  for(uint8_t i=0; i < LEDCOUNT; i++){
                      digitalWrite(ledPins[i], LOW);
                  }
                  // Done flashing.
                  // Now check Pattern.
                  if(!checkPatternMatch(patternIndex)) {
                      // failed check, 
                      // Save scoe and exit.
                      if(highScore < patternLength-1){
                        highScore = patternLength-1;
                      }
                      #ifdef USEEEPROM
                      saveHighScoreToEEPROM();
                      #endif
                      mode = SLEEPMODE;
                      flashFailedPattern();
                      // Leave GameMODE;
                      return;
                  }
                  //check passed, break out of while loop to for loop
                  break;
              }
              // TODO: If 1 minute has passed and no buttons pressed, leave game.
              // If no buttons pressed, loop again.
              delay(loopDelay);
          }
          // Wait for user to let go of button
          while(true){
              checkButtonsPressed();
              if(!anyButtonsPressed()){
                // Let the user take their hand off the button.
                  break;
              }
          }
      }
      // If the user got the pattern correct, 
      // Increment index and loop.
      patternLength++;
      // Give the user time to expect the next pattern.
      delay(1000);
  }
}

void handleResetTimer(){
  unsigned long startTime = millis();
  while(true){
    checkButtonsPressed();
    if(!allButtonsPressed()){
      mode = SLEEPMODE;
      // If buttons are released, leave reset Timer.
      return;
    }
    if(millis() - startTime >= resetholdTime){
      // Onces we're out of time, break the loop
      break;
    }
  }
  showClearing();
  // Reset Settings to Default.
  settings[0] = true;
  settings[1] = true;
  settings[2] = true;
  settings[3] = true;
  highScore = 0;

  #ifdef USEEEPROM
  saveSettingsToEEPROM();
  EEPROM.write(EEPROMhighScoreAddress, 0);
  #endif
  mode = SLEEPMODE;
  delay(loopDelay);
}

void handleLightShowMode(){
  turnOffLights();
  // Do cool light patterns to make brain happy!
  uint8_t LS_index = 0; // which pattern are we doing
  while(true){
    LS_index = random(0,2);
    checkButtonsPressed();
    if(outsideButtonsPressed()){
        // If both outside buttons are pressed, leave to sleepmode.
        mode = SLEEPMODE;
        return;
    }
    switch(LS_index){
      case 0:
        lightShowPattern1();
        break;
      case 1:
        lightShowPattern2();
        break;
    }
    delay(loopDelay);
  }
}

/*******************************
******** BUTTON HELPERS ********
*******************************/
void checkButtonsPressed(){
  for (uint8_t i = 0; i < BUTTONCOUNT; i++) {
    buttonsPressed[i] = (digitalRead(buttonPins[i]) == LOW);
  }
}
bool allButtonsPressed(){
  bool result = true;
  for (uint8_t i = 0; i < BUTTONCOUNT; i++) {
    if(!buttonsPressed[i]){
      result = false;
    }
  }
  return result;
}
bool outsideButtonsPressed(){
  bool result = true;
  if(!buttonsPressed[0]){
    result = false;
  }
  if(!buttonsPressed[BUTTONCOUNT-1]){
    result = false;
  }
  return result;
}
bool insideButtonsPressed(){
  bool result = true;
  for(uint8_t i =1; i < BUTTONCOUNT-1; i++){
    if(!buttonsPressed[i]){
      result = false;
    }
  }
  return result;
}
bool anyButtonsPressed(){
  bool result = false;
  for (uint8_t i = 0; i < BUTTONCOUNT; i++) {
    if(buttonsPressed[i]){
      result = true;
    }
  }
  return result;
}

bool checkPatternMatch(uint8_t patternIndex){
  // Check if user pressed the correct button in the current place in the pattern.
  bool pressedCorrect = false;
  bool pressedExtra = false;
  for (uint8_t i = 0; i < BUTTONCOUNT; i++) {
    if(pattern[patternIndex] == i){
      if(buttonsPressed[i]){
        pressedCorrect = true;
      }
    }else{
      if(buttonsPressed[i]){
        pressedExtra = true;
      }
    }
  }
  return (pressedCorrect & !pressedExtra);
}

/*******************************
********* LED HELPERS **********
*******************************/
void turnOffLights(){
  for(uint8_t i = 0; i < LEDCOUNT; i++){
      digitalWrite(ledPins[i], LOW);
  }
}
void flashPattern(){
  int patternPulse = settings[1] ? patternPulseSlow:patternPulseQuick;
  for (uint8_t i = 0; i < patternLength; i++) {
      digitalWrite(ledPins[pattern[i]], HIGH);
      delay(patternPulse);
      digitalWrite(ledPins[pattern[i]], LOW);
      delay(patternPulse);
  }
}

void flashFailedPattern(){
  for(uint8_t j = 0; j<2; j++){
    for(uint8_t i = 0; i<LEDCOUNT; i++){
      digitalWrite(ledPins[i], HIGH);
    }
    delay(500);
    for(uint8_t i = 0; i<LEDCOUNT; i++){
      digitalWrite(ledPins[i], LOW);
    }
    delay(500);
  }
}


void displayHighScore(){
  for (uint8_t i = 0; i < LEDCOUNT; i++) {
    if (highScore & (1 << i)) {
      digitalWrite(ledPins[i], HIGH);
    } else {
      digitalWrite(ledPins[i], LOW);
    }
  }
}

void displaySettings(){
  for (uint8_t i = 0; i < SETTINGSCOUNT; i++) {
    if(settings[i]){
      digitalWrite(ledPins[i], HIGH);
    } else {
      digitalWrite(ledPins[i], LOW);
    }
  }
}

void showSweep(){
  turnOffLights();
  // Show a cool Sweep sequence in the LEDs
  digitalWrite(ledPins[0], HIGH);
  delay(200);
  digitalWrite(ledPins[1], HIGH);
  delay(200);
  digitalWrite(ledPins[2], HIGH);
  delay(200);
  digitalWrite(ledPins[3], HIGH);
  delay(200);
  digitalWrite(ledPins[0], LOW);
  delay(200);
  digitalWrite(ledPins[1], LOW);
  delay(200);
  digitalWrite(ledPins[2], LOW);
  delay(200);
  digitalWrite(ledPins[3], LOW);
}
void showSweep2(){
  turnOffLights();
  // Show a cool Sweep sequence in the LEDs
  digitalWrite(ledPins[0], HIGH);
  digitalWrite(ledPins[3], HIGH);
  delay(200);
  digitalWrite(ledPins[2], HIGH);
  digitalWrite(ledPins[1], HIGH);
  delay(200);
  digitalWrite(ledPins[0], LOW);
  digitalWrite(ledPins[3], LOW);
  delay(200);
  digitalWrite(ledPins[2], LOW);
  digitalWrite(ledPins[1], LOW);
}

void showStartup(){
  // Show a cool startup sequence in the LEDs
  digitalWrite(ledPins[0], HIGH);
  delay(200);
  digitalWrite(ledPins[3], HIGH);
  delay(200);
  digitalWrite(ledPins[1], HIGH);
  delay(200);
  digitalWrite(ledPins[2], HIGH);
  delay(200);
  digitalWrite(ledPins[0], LOW);
  delay(200);
  digitalWrite(ledPins[3], LOW);
  delay(200);
  digitalWrite(ledPins[1], LOW);
  delay(200);
  digitalWrite(ledPins[2], LOW);
}
void showClearing(){
  turnOffLights();
  // Show a cool Sweep sequence in the LEDs
  digitalWrite(ledPins[0], HIGH);
  digitalWrite(ledPins[3], HIGH);
  digitalWrite(ledPins[2], HIGH);
  digitalWrite(ledPins[1], HIGH);
  delay(100);
  digitalWrite(ledPins[0], LOW);
  digitalWrite(ledPins[3], LOW);
  digitalWrite(ledPins[2], LOW);
  digitalWrite(ledPins[1], LOW);
  delay(100);
  digitalWrite(ledPins[0], HIGH);
  digitalWrite(ledPins[3], HIGH);
  digitalWrite(ledPins[2], HIGH);
  digitalWrite(ledPins[1], HIGH);
  delay(100);
  digitalWrite(ledPins[0], LOW);
  digitalWrite(ledPins[3], LOW);
  digitalWrite(ledPins[2], LOW);
  digitalWrite(ledPins[1], LOW);
  delay(100);
  digitalWrite(ledPins[0], HIGH);
  digitalWrite(ledPins[3], HIGH);
  digitalWrite(ledPins[2], HIGH);
  digitalWrite(ledPins[1], HIGH);
  delay(100);
  digitalWrite(ledPins[0], LOW);
  digitalWrite(ledPins[3], LOW);
  digitalWrite(ledPins[2], LOW);
  digitalWrite(ledPins[1], LOW);
  delay(100);
  digitalWrite(ledPins[0], HIGH);
  digitalWrite(ledPins[3], HIGH);
  digitalWrite(ledPins[2], HIGH);
  digitalWrite(ledPins[1], HIGH);
  delay(700);
  digitalWrite(ledPins[0], LOW);
  digitalWrite(ledPins[3], LOW);
  digitalWrite(ledPins[2], LOW);
  digitalWrite(ledPins[1], LOW);
}

void lightShowPattern1(){
// void lightShowPattern1(uint8_t sweeps = 2){
  uint8_t sweeps = 2;
  // night rider
  for(uint8_t i = 0; i < sweeps; i++){
    // Show a cool Sweep sequence in the LEDs
    digitalWrite(ledPins[0], HIGH);
    delay(200);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[1], HIGH);
    delay(200);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[2], HIGH);
    delay(200);
    digitalWrite(ledPins[2], LOW);
    digitalWrite(ledPins[3], HIGH);
    delay(200);
    digitalWrite(ledPins[3], LOW);
    digitalWrite(ledPins[2], HIGH);
    delay(200);
    digitalWrite(ledPins[2], LOW);
    digitalWrite(ledPins[1], HIGH);
    delay(200);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[0], HIGH);
    delay(200);
    digitalWrite(ledPins[0], LOW);
  }
}

void lightShowPattern2(){
// void lightShowPattern2(uint8_t flashes = 20);
// void lightShowPattern2(uint8_t flashes = 20, uint8_t flash_delay = 100){
  uint8_t flashes = 20;
  uint8_t flash_delay = 200;

  // random
  uint8_t led = 0;
  for(uint8_t i = 0; i < flashes; i++){
    led = random(1,4); // Pick a random LED
    digitalWrite(ledPins[led], HIGH);
    delay(flash_delay);
    digitalWrite(ledPins[led], LOW);
  }
}
void lightShowPattern3(){
  // out to in
  uint8_t loops = 3;
  for(uint8_t i = 0; i < loops; i++){
    //outside flash
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(100);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    delay(100);
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(100);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    //inside flash
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(100);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    delay(100);
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(100);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
  }
}
void lightShowPattern4(){
  // out to in
  uint8_t loops = 3;
  for(uint8_t i = 0; i < loops; i++){
    //odd flash
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(100);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    delay(100);
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(100);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    //even flash
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(100);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    delay(100);
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(100);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
  }
}



#ifdef USEEEPROM
/*******************************
******* EEPROM HELPERS *********
*******************************/
void saveHighScoreToEEPROM(){
    // Read in current Settings for comparison.
  uint8_t StoredHighScore = EEPROM.read(EEPROMhighScoreAddress);
  // Avoid extra uneccessary writes to EEPROM.
  if(StoredHighScore < highScore){
    EEPROM.write(EEPROMhighScoreAddress, highScore);
  }
}
void readHighScoreFromEEPROM(){
    // Read in current Settings for comparison.
  highScore = EEPROM.read(EEPROMhighScoreAddress);
  if(highScore > sizeof(pattern)){
    // EEPROM is default to all 1s, so set it back to zero.
    highScore = 0;
    EEPROM.write(EEPROMhighScoreAddress, 0);
  }
}

void saveSettingsToEEPROM() {
  // Pack settings into a single Byte.
  byte packedByte = 0;
  for (uint8_t i = 0; i < SETTINGSCOUNT; i++) {
    if (settings[i]) {
      packedByte |= (1 << i);
    }
  }
  // Read in current Settings for comparison.
  byte storedByte = EEPROM.read(EEPROMsettingsAddress);
  // Avoid extra uneccessary writes to EEPROM.
  if(storedByte != packedByte){
    EEPROM.write(EEPROMsettingsAddress, packedByte);
  }
}

void readSettingsFromEEPROM() {
  byte packedByte = EEPROM.read(EEPROMsettingsAddress);
  for (uint8_t i = 0; i < SETTINGSCOUNT; i++) {
    settings[i] = (packedByte & (1 << i)) != 0;
  }
}
#endif


#ifdef USEI2C
/*******************************
******** I2C HELPERS ***********
*******************************/
// Reference: https://github.com/lukejenkins/minibadge/blob/master/I2C%20Example%20Code/README.md
void requestI2CReadEvent() {
  Wire.write(0x02); // Say we are responding with text.
  //Wire.write(sizeof(highscore)); // Send the length of the message
  Wire.write(0x01); // Only a single byte for the score?
  Wire.write(highScore); // Send the current score as a byte
}
void requestI2CWriteEvent(){
  // No Write support
  Wire.write(0x00); // Respond with "no write support".
}
#endif