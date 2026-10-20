#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>
#include <EEPROM.h>

// Pins
const int trigPin   = 10;
const int echoPin   = 11;
const int buzzer    = 12;
const int servoPin  = A3;
const int buttonPin = A2;
const int greenLED  = A0;
const int redLED    = A1;

// Settings
const int  TRIGGER_DISTANCE_CM = 20;
const byte READINGS_TO_TRIGGER = 3;          // close readings in a row before alarm
const unsigned long SENSOR_INTERVAL = 100;   // ms between sensor pings
const unsigned long EXIT_DELAY      = 5000;  // time to step away after arming
const unsigned long ENTRY_DELAY     = 10000; // time to type the code after motion
const unsigned long ENTRY_TIMEOUT   = 8000;  // clear half-typed code after this
const byte MAX_ATTEMPTS = 3;
const unsigned long LOCKOUT_TIMES[] = {10000, 30000, 60000};  // gets longer each time
const byte LOCKOUT_LEVELS = 3;
const byte MIN_CODE_LEN = 4;
const byte MAX_CODE_LEN = 8;
const bool HIDE_CODE = false;                // true = show **** instead of digits

// EEPROM layout for the saved code
const int  EE_MAGIC_ADDR = 0;
const byte EE_MAGIC      = 0x42;
const int  EE_LEN_ADDR   = 1;
const int  EE_CODE_ADDR  = 2;

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;

// Keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Passwords
const String DEFAULT_ARM_PASSWORD = "1234";
const String ALERT_PASSWORD       = "2468";
String armPassword = DEFAULT_ARM_PASSWORD;   // loaded from EEPROM at startup
String entered = "";
String newCode = "";

// System states
enum State { DISARMED, ARMED, ENTRY, ALERT, CONFIRM, CHANGE_OLD, CHANGE_NEW, CHANGE_REPEAT };
State state = ARMED;

bool resetRequested = false;   // button pressed during alarm -> arm code resets it
byte failedAttempts = 0;
byte lockoutLevel = 0;
bool lockedOut = false;
unsigned long lockoutStart = 0;
unsigned long lockoutDuration = 0;
unsigned long lastKeyTime = 0;
unsigned long armedAt = 0;
unsigned long entryStart = 0;
unsigned long alertStart = 0;
unsigned long lastSensorTime = 0;
byte closeReadings = 0;
unsigned long messageStart = 0;
unsigned long messageDuration = 0;   // 0 = no temporary message on screen
bool notifiedPending = false;
bool lastButtonState = HIGH;

// ---------- Sounds ----------

struct Note { unsigned int freq; unsigned int ms; };
const Note CLICK[] = {{2000, 30}};
const Note GOOD[]  = {{1500, 80}, {2000, 80}, {2600, 120}};
const Note BAD[]   = {{400, 200}, {250, 400}};
const Note DENY[]  = {{200, 80}};
const Note WARN[]  = {{1000, 100}};

const Note* melody = nullptr;
byte melodyLen = 0;
byte melodyIndex = 0;
unsigned long noteStart = 0;
bool sirenOn = false;
unsigned long lastSirenUpdate = 0;
unsigned long lastWarnBeep = 0;

#define PLAY(m) playSound(m, sizeof(m) / sizeof(m[0]))

void playSound(const Note* m, byte len) {
  melody = m;
  melodyLen = len;
  melodyIndex = 0;
  noteStart = millis();
  tone(buzzer, m[0].freq, m[0].ms);
}

// ---------- Setup & loop ----------

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);

  lcd.init();
  lcd.backlight();
  myServo.attach(servoPin);
  myServo.write(0);
  delay(10);

  // Hold the button while powering on to reset a forgotten code
  if (digitalRead(buttonPin) == LOW) {
    armPassword = DEFAULT_ARM_PASSWORD;
    saveCode();
    printLine(0, "Code Reset To");
    printLine(1, "Default: " + DEFAULT_ARM_PASSWORD);
    delay(2000);
  } else {
    loadCode();
  }

  printLine(0, "Security System");
  printLine(1, "Starting...");
  delay(2000);   // the only other blocking delay, at boot

  lastButtonState = digitalRead(buttonPin);
  setState(ARMED);
  showStatus();
}

void loop() {
  checkButton();
  checkKeypad();
  checkSensor();
  updateEntryDelay();
  updateSound();
  updateLEDs();
  updateScreenTimers();
}

void setState(State s) {
  bool sirenState = (s == ALERT || s == CONFIRM);
  if (sirenOn && !sirenState) {
    noTone(buzzer);
    sirenOn = false;
  }
  if (s == ALERT && state != CONFIRM) {
    alertStart = millis();
    resetRequested = false;
  }
  if (s == ARMED) armedAt = millis();
  if (s == ENTRY) {
    entryStart = millis();
    lastWarnBeep = 0;
  }

  state = s;
  entered = "";
  closeReadings = 0;
  myServo.write((s == ARMED || s == ENTRY) ? 0 : 90);
}

bool isChangeState() {
  return state == CHANGE_OLD || state == CHANGE_NEW || state == CHANGE_REPEAT;
}

// ---------- Inputs ----------

void checkButton() {
  static unsigned long lastChange = 0;
  bool reading = digitalRead(buttonPin);
  if (reading != lastButtonState && millis() - lastChange > 50) {
    lastChange = millis();
    lastButtonState = reading;
    if (reading == LOW) {
      onButtonPress();
    }
  }
}

void onButtonPress() {
  switch (state) {
    case DISARMED:
      // Arming is always safe, so no code needed
      setState(ARMED);
      PLAY(GOOD);
      showMessage("Button Pressed", "System Armed", 1500);
      break;
    case ALERT:
      // Silencing the alarm needs the arm code
      resetRequested = true;
      entered = "";
      PLAY(CLICK);
      showStatus();
      break;
    default:
      PLAY(DENY);
      showMessage("Button Locked", "Use the Code", 1500);
      break;
  }
}

void checkKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  if (lockedOut) {
    PLAY(DENY);
    return;
  }

  lastKeyTime = millis();
  PLAY(CLICK);

  if (state == CONFIRM) {
    handleConfirmation(key);
  } else {
    handleCodeKey(key);
  }
}

void checkSensor() {
  if (state != ARMED) return;
  if (millis() - armedAt < EXIT_DELAY) return;
  if (millis() - lastSensorTime < SENSOR_INTERVAL) return;
  lastSensorTime = millis();

  long distance = getDistance();
  if (distance > 0 && distance < TRIGGER_DISTANCE_CM) {
    closeReadings++;
    if (closeReadings >= READINGS_TO_TRIGGER) {
      setState(ENTRY);
      messageDuration = 0;
      showStatus();
    }
  } else {
    closeReadings = 0;
  }
}

long getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) return -1;   // no echo
  return duration / 58;           // cm
}

// ---------- Keypad logic ----------

void handleCodeKey(char key) {
  if (key == '*') {
    if (entered.length() == 0 && isChangeState()) {
      setState(DISARMED);
      showMessage("Code Change", "Cancelled", 1500);
      return;
    }
    entered = "";
    showStatus();
    return;
  }

  if (key == 'A' && state == DISARMED && entered.length() == 0) {
    setState(CHANGE_OLD);
    messageDuration = 0;
    showStatus();
    return;
  }

  if (key == '#') {
    if (entered.length() > 0) submitCode();
    return;
  }

  if (entered.length() < MAX_CODE_LEN) {
    entered += key;
    messageDuration = 0;
    notifiedPending = false;
    showStatus();
  }
}

void submitCode() {
  String code = entered;
  entered = "";

  switch (state) {
    case ALERT:
      if (code == ALERT_PASSWORD) {
        codeAccepted();
        setState(CONFIRM);
        showStatus();
      } else if (resetRequested && code == armPassword) {
        codeAccepted();
        setState(ARMED);
        showMessage("Alarm Reset", "System Armed", 1500);
      } else {
        wrongCode();
      }
      break;

    case DISARMED:
    case ARMED:
    case ENTRY:
      if (code == armPassword) {
        bool arming = (state == DISARMED);
        codeAccepted();
        setState(arming ? ARMED : DISARMED);
        showMessage("Access Granted", arming ? "System Armed" : "System Disarmed", 1500);
      } else {
        wrongCode();
      }
      break;

    case CHANGE_OLD:
      if (code == armPassword) {
        codeAccepted();
        setState(CHANGE_NEW);
        showStatus();
      } else {
        wrongCode();
      }
      break;

    case CHANGE_NEW:
      if (code.length() < MIN_CODE_LEN || !allDigits(code)) {
        PLAY(BAD);
        showMessage("Use 4-8 Digits", "Try Again", 1500);
      } else if (code == ALERT_PASSWORD) {
        PLAY(BAD);
        showMessage("Can't Use That", "Try Again", 1500);
      } else {
        newCode = code;
        setState(CHANGE_REPEAT);
        showStatus();
      }
      break;

    case CHANGE_REPEAT:
      if (code == newCode) {
        armPassword = newCode;
        saveCode();
        PLAY(GOOD);
        setState(DISARMED);
        showMessage("Code Saved!", "New Code Active", 2000);
      } else {
        PLAY(BAD);
        setState(CHANGE_NEW);
        showMessage("No Match", "Start Again", 1500);
      }
      break;

    default:
      break;
  }
}

void handleConfirmation(char key) {
  if (key == '1') {
    setState(DISARMED);
    PLAY(GOOD);
    showMessage("False Alarm", "System Disarmed", 2000);
  } else if (key == '2') {
    setState(DISARMED);
    showMessage("Calling 911...", "", 2000);
    notifiedPending = true;   // shows "Authorities Notified" next
  }
}

void codeAccepted() {
  failedAttempts = 0;
  lockoutLevel = 0;
  PLAY(GOOD);
}

void wrongCode() {
  failedAttempts++;
  PLAY(BAD);
  if (failedAttempts >= MAX_ATTEMPTS) {
    failedAttempts = 0;
    lockoutDuration = LOCKOUT_TIMES[lockoutLevel];
    if (lockoutLevel < LOCKOUT_LEVELS - 1) lockoutLevel++;
    lockedOut = true;
    lockoutStart = millis();
    if (isChangeState()) setState(DISARMED);
    showMessage("Too Many Tries", "Locked " + String(lockoutDuration / 1000) + " sec", 2000);
  } else {
    showMessage("Wrong Password", "Tries: " + String(failedAttempts) + "/" + String(MAX_ATTEMPTS), 1500);
  }
}

bool allDigits(const String &s) {
  for (unsigned int i = 0; i < s.length(); i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

// ---------- Saved code (EEPROM) ----------

void loadCode() {
  if (EEPROM.read(EE_MAGIC_ADDR) != EE_MAGIC) return;   // nothing saved yet
  byte len = EEPROM.read(EE_LEN_ADDR);
  if (len < MIN_CODE_LEN || len > MAX_CODE_LEN) return;
  String code = "";
  for (byte i = 0; i < len; i++) {
    code += (char)EEPROM.read(EE_CODE_ADDR + i);
  }
  if (allDigits(code)) armPassword = code;
}

void saveCode() {
  EEPROM.update(EE_LEN_ADDR, armPassword.length());
  for (byte i = 0; i < armPassword.length(); i++) {
    EEPROM.update(EE_CODE_ADDR + i, armPassword[i]);
  }
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
}

// ---------- Alarm, sound & LEDs ----------

void triggerAlert() {
  setState(ALERT);
  notifiedPending = false;
  showMessage("!! ALERT !!", "Intruder Alert!", 3000);
}

void updateEntryDelay() {
  if (state != ENTRY) return;
  unsigned long elapsed = millis() - entryStart;
  if (elapsed >= ENTRY_DELAY) {
    triggerAlert();
    return;
  }
  // Warning beeps, faster in the last 3 seconds
  unsigned long beepEvery = (ENTRY_DELAY - elapsed < 3000) ? 250 : 1000;
  if (melody == nullptr && millis() - lastWarnBeep >= beepEvery) {
    lastWarnBeep = millis();
    PLAY(WARN);
  }
}

void updateSound() {
  // Siren: sweeps 600 Hz -> 1600 Hz -> 600 Hz every second
  if (state == ALERT || state == CONFIRM) {
    melody = nullptr;
    if (millis() - lastSirenUpdate >= 10) {
      lastSirenUpdate = millis();
      unsigned long phase = (millis() - alertStart) % 1000;
      unsigned int freq = (phase < 500) ? 600 + phase * 2 : 600 + (1000 - phase) * 2;
      tone(buzzer, freq);
      sirenOn = true;
    }
    return;
  }

  // Short sound effects
  if (melody != nullptr && millis() - noteStart >= melody[melodyIndex].ms + 20) {
    melodyIndex++;
    if (melodyIndex >= melodyLen) {
      melody = nullptr;
    } else {
      noteStart = millis();
      tone(buzzer, melody[melodyIndex].freq, melody[melodyIndex].ms);
    }
  }
}

void updateLEDs() {
  bool blink = (millis() / 250) % 2;
  bool red = (state == ALERT || state == CONFIRM) || (state == ENTRY && blink);
  digitalWrite(greenLED, state == ARMED ? HIGH : LOW);  // Armed = green
  digitalWrite(redLED, red ? HIGH : LOW);               // Entry = blinking red, Alert = red
  // Disarmed = both off
}

// ---------- Screen ----------

// Prints a full 16-char line (pads with spaces, no flicker from lcd.clear())
void printLine(byte row, const String &text) {
  lcd.setCursor(0, row);
  lcd.print(text);
  for (int i = text.length(); i < 16; i++) {
    lcd.print(' ');
  }
}

void showMessage(const String &line1, const String &line2, unsigned long ms) {
  printLine(0, line1);
  printLine(1, line2);
  messageStart = millis();
  messageDuration = ms;
}

unsigned long entrySecsLeft() {
  unsigned long elapsed = millis() - entryStart;
  if (elapsed >= ENTRY_DELAY) return 0;
  return (ENTRY_DELAY - elapsed + 999) / 1000;
}

unsigned long lockSecondsLeft() {
  unsigned long elapsed = millis() - lockoutStart;
  if (elapsed >= lockoutDuration) return 0;
  return (lockoutDuration - elapsed + 999) / 1000;
}

String topLine() {
  switch (state) {
    case CONFIRM:       return "1 = False Alarm";
    case CHANGE_OLD:    return "Change: Old Code";
    case CHANGE_NEW:    return "Change: New Code";
    case CHANGE_REPEAT: return "Repeat New Code";
    case ENTRY:         return "Motion! " + String(entrySecsLeft()) + "s left";
    case ALERT:         return "!! ALERT !!";
    case ARMED:         return lockedOut ? "System Locked" : "System Armed";
    default:            return lockedOut ? "System Locked" : "System Disarmed";
  }
}

String bottomLine() {
  if (state == CONFIRM) return "2 = Real Threat";
  if (lockedOut) return "Wait " + String(lockSecondsLeft()) + " sec";

  if (entered.length() > 0) {
    String shown = entered;
    if (HIDE_CODE) {
      shown = "";
      for (unsigned int i = 0; i < entered.length(); i++) shown += '*';
    }
    return "Code: " + shown;
  }

  switch (state) {
    case ALERT:    return resetRequested ? "Code to Reset" : "Enter Alert Code";
    case DISARMED: return "Code / A=Change";
    case CHANGE_OLD:
    case CHANGE_NEW:
    case CHANGE_REPEAT: return "# = OK  * = Back";
    default:       return "Enter Code";
  }
}

void showStatus() {
  printLine(0, topLine());
  printLine(1, bottomLine());
}

void updateScreenTimers() {
  // Temporary message finished -> back to the normal screen
  if (messageDuration > 0 && millis() - messageStart >= messageDuration) {
    messageDuration = 0;
    if (notifiedPending) {
      notifiedPending = false;
      showMessage("Authorities", "Notified", 3000);
    } else {
      showStatus();
    }
  }

  // Half-typed code timed out
  if (entered.length() > 0 && millis() - lastKeyTime >= ENTRY_TIMEOUT) {
    entered = "";
    showStatus();
  }

  // Lockout finished
  if (lockedOut && millis() - lockoutStart >= lockoutDuration) {
    lockedOut = false;
    if (messageDuration == 0) showStatus();
  }

  // Keep countdowns on screen up to date
  static unsigned long lastRedraw = 0;
  if ((lockedOut || state == ENTRY) && messageDuration == 0 && millis() - lastRedraw >= 250) {
    lastRedraw = millis();
    showStatus();
  }
}
