// The I2C bus allows the Main badge to communicate with this minibadge 
// to pull/set settings, high scores, etc.
#define USEI2C true

// Using the EEPROM allows the badge to retain highscore and settings when powered off. 
#define USEEEPROM true

// Checks for certain compiler packages.
#ifdef PINMAPPING_CCW
#error "Sketch was written for clockwise pin mapping!"
#endif


/*******************************
******* GENERAL SETTINGS *******
********************************/
// Pin definitions
#define BUTTONCOUNT 4
#define LEDCOUNT 4
// const uint8_t ledPins[LEDCOUNT] = {10, 9, 8, 7}; // LED pins (v1,v2 minibadge only)
// const uint8_t buttonPins[BUTTONCOUNT] = {1, 2, 3, 4}; // Button pins (v1,v2 minibadge only)
// const uint8_t ledPins[LEDCOUNT] = {10, 11, 12, 13}; // LED pins (v3+ minibadge)
const uint8_t ledPins[LEDCOUNT] = {3, 2, 1, 0}; // LED pins (v3+ minibadge)
const uint8_t buttonPins[BUTTONCOUNT] = {7, 8, 9, 10}; // Button pins (v3+ minibadge)
const uint8_t clockPin = 5; // Clock Sensing pin. (v3+ minibadge)

// Variables for menu settings
#define SETTINGSCOUNT 4 // Settings count should always be <= button and LED counts, and should never exceed 8.
bool settings[SETTINGSCOUNT] = {true, true, true, true}; // {showScoreWhileAsleep, (non)FastMode, (non)ChaoticMode, PartyModeOnBoot }
bool buttonsPressed[BUTTONCOUNT] = {false, false, false, false}; // Buttons Pressed State.

// Variables for transitions and debounce
const unsigned int loopDelay = 200; // Give users time to press buttons between loops
const unsigned long resetholdTime = 10000; // 10 seconds in milliseconds
const unsigned long gameTimeout = 60000;  // Wait 1 minute for a button to be pressed.

// Track highest score
volatile uint32_t highScore = 0;

// Variables for game state
uint8_t pattern[24]; // Max pattern length (Should this be longer than 16?)
uint8_t patternLength = 0; // Current pattern Index (a.k.a current score)
int patternPulseSlow = 500; // How long to show each pattern step for.
int patternPulseQuick = 100; // How long to show each pattern step for.


// Tracking the current mode the badge is in. 
enum Modes {SLEEPMODE, SETTINGSMODE, GAMEMODE, LIGHTSHOWMODE}; 
Modes mode = SLEEPMODE;


#ifdef USEEEPROM
/*******************************
********** EEPROM **************
********************************/
#include <EEPROM.h>
// EEPROM address for high score
const uint8_t EEPROMhighScoreAddress = 0;
const uint8_t EEPROMsettingsAddress = 8;
#endif

#ifdef USEI2C
/*******************************
************ I2C ***************
********************************/
// Note that the ATTiny Core's implmentation of wire.h is used
// ref: https://github.com/SpenceKonde/ATTinyCore/blob/v2.0.0-devThis-is-the-head-submit-PRs-against-this/avr/libraries/Wire/src/Wire.h
// ref: https://github.com/lukejenkins/minibadge/blob/master/I2C%20Example%20Code/Minibadge_sample_code.ino
#include <Wire.h>
#define I2C_DEVICE_ADDR 0x23 // I2C address for this device
uint8_t I2C_WRITE_SUPPORT = 1; // Set to 1 to enable Writing Score and settings
// Writing Actions:
#define I2C_WRITE_SCORE 0
#define I2C_WRITE_SETTING_1 1
#define I2C_WRITE_SETTING_2 2
#define I2C_WRITE_SETTING_3 3
#define I2C_WRITE_SETTING_4 4
#define I2C_WRITE_BRIGHTNESS 5
// Reading Actions:
#define I2C_READ_NOP 0 // No operation
#define I2C_READ_BUTTON 1 // toggling button
#define I2C_READ_SCORE 2 // Reading High Score
// #define I2C_READ_SETTING_1 20 // Reading Setting 1
// #define I2C_READ_SETTING_2 30 // Reading Setting 2
// #define I2C_READ_SETTING_3 40 // Reading Setting 3
// #define I2C_READ_SETTING_4 50 // Reading Setting a4
// #define I2C_READ_BRIGHTNESS 60 // Reading LED Brightness
uint8_t i2cReadAction = I2C_READ_SCORE;


// Any variable being writen to inside an interupt Ie. request() or recieve() should be volatile.
// These are default values and will change as the badge talks to the minibadge.
// enum ReadStates { I2C_STATE_NOP, RespondWrite, RespondRead, ReadPartTwo, ReadPartThree };
// #define I2C_STATE_NOP 0
// #define I2C_STATE_WRITE 1
// #define I2C_STATE_READ 2
// #define I2C_STATE_READ_PT2 3 // Rading High Score
// #define I2C_STATE_READ_PT3 4 // Rading High Score

enum ReadStates { I2C_STATE_NOP, I2C_STATE_WRITE, I2C_STATE_READ, I2C_STATE_READ_PT2, I2C_STATE_READ_PT3 };
volatile ReadStates i2cState = I2C_STATE_NOP;
// volatile ReadStates reading_state = I2C_STATE_NOP; // this should only be set to RespondWrite or RespondRead in the recieve function.
volatile uint8_t brightness = 100;
#endif

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
  
  pinMode(clockPin, INPUT);

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
  Wire.onRequest(RequestI2CEvent); // Register event handler for I2C Request events
  Wire.onReceive(RecieveI2CEvent); // Register event handler for I2C Recieve events
  Wire.begin(I2C_DEVICE_ADDR); // Join I2C bus with the given address
#else
  // Set I2C pins to read so they do not interfere with the rest of the bus
  // Pins 6&5 for the ATTINY84a.
  pinMode(6, INPUT); // SDA
  pinMode(5, INPUT); // SCL
#endif

  // Show Startup Sequence
  showStartup();

  // Set initial state
  if(settings[3]){
    mode = SLEEPMODE;
  }else{
    mode = LIGHTSHOWMODE;
  }
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
  // randomSeed(analogRead(unconnectedPin));
  randomSeed(micros());

  // Turn off all LEDs
  turnOffLights();

  while(true){
      if(!settings[2]){
        // if Chaotic Mode, build a new pattern each time.
        for(uint8_t i = 0; i <= patternLength; i++){
          pattern[i] = (uint8_t)random(0, 4); // from 1 to 4;
        }
      }else{
        // When not in Chatoic Mode, just add a new value to the end of the pattern
        pattern[patternLength] = (uint8_t)random(0, 4); // from 1 to 4;
      }
      flashPattern();
      // Have user input all parts of pattern.
      for(uint8_t patternIndex=0; patternIndex < patternLength; patternIndex++){
          // loop forever while waiting for user to press a button
          unsigned long lastButtonTime = millis();
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
                  turnOffLights();
                  // Done flashing.
                  // Now check Pattern.
                  if(!checkPatternMatch(patternIndex)) {
                      // failed check, 
                      // Save scoe and exit.
                      if(highScore < patternLength){
                        highScore = patternLength;
                      }
                      #ifdef USEEEPROM
                      saveHighScoreToEEPROM();
                      #endif
                      flashFailedPattern();
                      mode = SLEEPMODE;
                      // Leave GameMODE;
                      return;
                  }
                  //check passed, break out of while loop to for loop
                  break;
              }
              delay(loopDelay);
          }
          // Wait for user to let go of button
          while(anyButtonsPressed()){
              checkButtonsPressed();
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
    LS_index = random(0,4);
    checkButtonsPressed();
    if(outsideButtonsPressed()){
        // If both outside buttons are pressed, leave to sleepmode.
        mode = SLEEPMODE;
        // Wait for user to let go of button
        while(anyButtonsPressed()){
            checkButtonsPressed();
        }
        return;
    }
    switch(LS_index){
      case 0:
        lightShowPattern1();
        break;
      case 1:
        lightShowPattern2();
        break;
      case 2:
        lightShowPattern3();
        break;
      case 3:
        lightShowPattern4();
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
  //TODO If Score is >16, then just show all lights on.
  // Alternatative: flash throught the full byte?
  uint32_t score = highScore;
  if (highScore >= 16){
    score = 0xFF;
  }
  for (uint8_t i = 0; i < LEDCOUNT; i++) {
    if (score & (1 << i)) {
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
  uint8_t flashes = 10;
  uint8_t flash_delay = 200;

  // random
  uint8_t led = 0;
  for(uint8_t i = 0; i < flashes; i++){
    led = random(0,4); // Pick a random LED
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
    delay(200);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    delay(200);
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(200);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    //inside flash
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(200);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    delay(200);
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(200);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
  }
}
void lightShowPattern4(){
  // every other flash
  uint8_t loops = 3;
  for(uint8_t i = 0; i < loops; i++){
    //odd flash
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(200);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    delay(200);
    digitalWrite(ledPins[0], HIGH);
    digitalWrite(ledPins[LEDCOUNT-2], HIGH);
    delay(200);
    digitalWrite(ledPins[0], LOW);
    digitalWrite(ledPins[LEDCOUNT-2], LOW);
    //even flash
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(200);
    digitalWrite(ledPins[1], LOW);
    digitalWrite(ledPins[LEDCOUNT-1], LOW);
    delay(200);
    digitalWrite(ledPins[1], HIGH);
    digitalWrite(ledPins[LEDCOUNT-1], HIGH);
    delay(200);
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
void RequestI2CEvent() {

  // If the badge is writing then i2cState will be RespondWrite from the recieve function.
  // This tells the badge wether this minibadge supports write events.
  if(i2cState == I2C_STATE_WRITE){
    Wire.write(I2C_WRITE_SUPPORT);
  // If i2cState is set to RespondRead then the badge is beginning the read request.
  }else if(i2cState == I2C_STATE_READ ){

    Wire.write(0x02); // Say we're responding with TEXT type.

    // // We will loop through the returned values every time a read is made. 
    // // This switch statement will handle the sending of each value for the current loop, 
    // // and setting up the action for the next loop.
    // // At the end of the loop, return a NOP to show that we're starting the responses over. 
    // // (length) --> (score) --> (setting1)  --> (setting2)  --> (setting3)  --> (setting4)  --> (brightness) --> (NOP) / loop
    // switch(i2cReadAction){
    //   // When 
    //   case I2C_STATE_NOP:
    //     // Send the i2cReadAction to the badge.
    //     Wire.write(i2cReadAction);
    //     i2cReadAction = I2C_READ_SCORE;
    //     break;
    //   case I2C_READ_SCORE:
    //     // Send the i2cReadAction to the badge.
    //     Wire.write(i2cReadAction);
    //     Wire.write(sizeof(highScore));
    //     Wire.write(highScore);
    //     i2cReadAction = I2C_READ_SCORE;
    //     break;
    // }


    // If it is 2 for a text message let this function know next read event will need to send the
    // message length by setting i2cState to ReadPartTwo.
    if(i2cReadAction == I2C_READ_SCORE){
      i2cState = I2C_STATE_READ_PT2;

    // If i2cReadAction is anything else then set i2cState to I2C_STATE_NOP to indicate the minibadge
    // should not respond with anything else till the next init sequence from the badge.
    }else{
      i2cState = I2C_STATE_NOP;
    }

    // It is a good idea to set the i2cReadAction to zero after any value is read.
    // This will prevent the minibadge from "spamming" its message if the main badge
    // is not checking for that.
    i2cReadAction = I2C_READ_NOP;

  // If i2cState is I2C_STATE_READPT2 then the minibadge should respond with the text message length and
  // advance i2cState to I2C_STATE_READPT3 to let the minibadge know next read should be the text message.
  }else if(i2cState == I2C_STATE_READ_PT2){
    // Wire.write(sizeof(highScore));
    Wire.write(0x01);
    i2cState = I2C_STATE_READ_PT3;

  // If i2cState is I2C_STATE_READPT3 then the minibadge should send the text message one byte at a time.
  // Once it is done it should set i2cState to I2C_STATE_READPT3 to let the minibadge know to do nothing
  // until the badge inits communication again.
  }else if(i2cState == I2C_STATE_READ_PT3){
    // for(uint8_t i = 0; i < highScore; i++){
      Wire.write(highScore);
    // }
    i2cState = I2C_STATE_NOP;
  }

}
// Handle Recieiving data on I2C bus.
// byteCount will store the number of bytes sent in the write event.
void RecieveI2CEvent(int byteCount){
  // This will read the first byte in the buffer into the byteOne variable.
  uint8_t byteOne = Wire.read();
  // If the first byte read is 0x00 then we know the minibadge initiation sequence was started.
  if(byteOne == 0x00){
    // This reads the next byte and will let us know if the badge wants to read (The byte is one)
    // or write to the minibadge (The byte is zero).
    i2cState = Wire.read() ? I2C_STATE_READ: I2C_STATE_WRITE;

  // If the first byte is anything other than 0x00 then we are handling a write event.
  // But if we don't support writing, then ignore the packet.
  }else if(I2C_WRITE_SUPPORT == 1){
    // The next bytes says which section/space to write to. 
    uint8_t writeSection = Wire.read();

    switch(writeSection){
      case I2C_WRITE_SCORE:
        // if this is a score update. The score is 32 bits long and in big endian.
        // Note that only a 4 bit score can be displayed on the screen, even though a larger score could be set.
        // highScore = (((uint32_t)Wire.read()) << 8) + (uint32_t)Wire.read();
        highScore = (((uint32_t)Wire.read()));
        break;
      case I2C_WRITE_BRIGHTNESS:
        // This is for a brightness update. The brightness range should be 0-127.
        brightness = Wire.read();
        break;
      case I2C_WRITE_SETTING_1:
        // if this is for setting 1, update the setting.
        settings[0] = Wire.read()>0?true:false;
        break;  
      case I2C_WRITE_SETTING_2:
        // if this is for setting 2, update the setting.
        settings[1] = Wire.read()>0?true:false;
        break;  
      case I2C_WRITE_SETTING_3:
        // if this is for setting 3, update the setting.
        settings[2] = Wire.read()>0?true:false;
        break;  
      case I2C_WRITE_SETTING_4:
        // if this is for setting 4, update the setting.
        settings[3] = Wire.read()>0?true:false;
        break;  
    }
  }
}
#endif