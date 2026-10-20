// Hardware self-test for the Arduino Keypad Security System.
// Upload this, open the Serial Monitor at 9600 baud, and follow along.
// It checks every part one by one, then runs a live test you can poke at.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>

// Same pins as the main sketch
const int trigPin   = 10;
const int echoPin   = 11;
const int buzzer    = 12;
const int servoPin  = A3;
const int buttonPin = A2;
const int greenLED  = A0;
const int redLED    = A1;

LiquidCrystal_I2C lcd(0x27, 16, 2);   // change to 0x3F if the scan below finds that
Servo myServo;

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

void setup() {
  Serial.begin(9600);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(greenLED, OUTPUT);
  pinMode(redLED, OUTPUT);

  Serial.println(F("=== Security System Self-Test ==="));

  // 1. I2C scan: finds the LCD's address
  Serial.println(F("[1] Scanning I2C bus..."));
  Wire.begin();
  byte found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("    Found device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("    Nothing found! Check SDA->A4, SCL->A5, VCC->5V, GND->GND."));
  }

  // 2. LCD
  Serial.println(F("[2] LCD: should say 'Self-Test OK'"));
  lcd.init();
  lcd.backlight();
  lcd.print(F("Self-Test OK"));
  lcd.setCursor(0, 1);
  lcd.print(F("0123456789ABCDEF"));
  delay(1500);

  // 3. LEDs
  Serial.println(F("[3] Green LED on"));
  digitalWrite(greenLED, HIGH);
  delay(800);
  digitalWrite(greenLED, LOW);
  Serial.println(F("    Red LED on"));
  digitalWrite(redLED, HIGH);
  delay(800);
  digitalWrite(redLED, LOW);

  // 4. Buzzer: three rising tones (a passive buzzer changes pitch, an active one won't)
  Serial.println(F("[4] Buzzer: low, middle, high tone"));
  tone(buzzer, 500, 250);  delay(350);
  tone(buzzer, 1000, 250); delay(350);
  tone(buzzer, 2000, 250); delay(350);

  // 5. Servo
  Serial.println(F("[5] Servo: 0 -> 90 -> 0 degrees"));
  myServo.attach(servoPin);
  myServo.write(0);   delay(600);
  myServo.write(90);  delay(600);
  myServo.write(0);   delay(600);

  Serial.println(F("[6] Live test: press keys, press the button, wave at the sensor."));
  Serial.println(F("    Green LED = button held, Red LED = something under 20 cm."));
  lcd.clear();
  lcd.print(F("Press a key..."));
}

void loop() {
  // Keypad
  char key = keypad.getKey();
  if (key) {
    Serial.print(F("Key: "));
    Serial.println(key);
    tone(buzzer, 2000, 30);
    lcd.setCursor(0, 0);
    lcd.print(F("Key: "));
    lcd.print(key);
    lcd.print(F("          "));
  }

  // Button
  static bool lastButton = HIGH;
  bool button = digitalRead(buttonPin);
  digitalWrite(greenLED, button == LOW ? HIGH : LOW);
  if (button != lastButton) {
    lastButton = button;
    Serial.println(button == LOW ? F("Button: PRESSED") : F("Button: released"));
  }

  // Ultrasonic sensor
  static unsigned long lastPing = 0;
  if (millis() - lastPing >= 500) {
    lastPing = millis();
    long distance = getDistance();
    digitalWrite(redLED, (distance > 0 && distance < 20) ? HIGH : LOW);

    lcd.setCursor(0, 1);
    lcd.print(F("Dist: "));
    if (distance < 0) lcd.print(F("--"));
    else lcd.print(distance);
    lcd.print(F(" cm      "));

    Serial.print(F("Distance: "));
    if (distance < 0) Serial.println(F("no echo"));
    else { Serial.print(distance); Serial.println(F(" cm")); }
  }
}

long getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) return -1;
  return duration / 58;
}
