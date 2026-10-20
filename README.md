# Arduino Keypad Security System

A small alarm system for the Arduino Uno. It watches a doorway with an ultrasonic sensor, gives you 10 seconds to type your code, then sounds a siren. It also has a servo "lock", lockouts for wrong guesses and a code you can change from the keypad.

**[View the full project page →](https://akshajtripathi24-ui.github.io/arduino-security-system/)** (wiring diagram, screen simulator, code walkthrough and troubleshooter)

## Features

- **Motion detection:** an HC-SR04 checks 10 times a second for anything closer than 20 cm, and needs 3 close readings in a row.
- **Exit and entry delays:** 5 seconds to leave after arming, and 10 seconds to type the code when motion is seen, with warning beeps that speed up.
- **Siren and sounds:** a 600–1600 Hz sweeping siren, key clicks, a chirp for a correct code and a buzz for a wrong one.
- **Lockouts:** 3 wrong codes lock the keypad for 10 s, then 30 s, then 60 s. The sensor and siren keep working.
- **Changeable code:** press `A` while disarmed. The code is saved in EEPROM and survives power cuts.
- **Alarm handling:** an alert code stops the siren and asks whether it was a false alarm or a real threat.
- **Servo lock:** 0° (locked) while armed, 90° (open) otherwise.
- **No `delay()` in the main loop**, so nothing freezes and no keypress is missed.

## Parts

| Part | Qty |
|---|---|
| Arduino Uno | 1 |
| 16×2 LCD with I2C backpack (address `0x27`) | 1 |
| 4×4 membrane keypad | 1 |
| HC-SR04 ultrasonic sensor | 1 |
| SG90 micro servo | 1 |
| **Passive** buzzer | 1 |
| Push button | 1 |
| Green + red LED | 2 |
| 220 Ω resistors | 2 |
| Breadboard + jumper wires | — |

**Libraries:** install `LiquidCrystal I2C` (Frank de Brabander) and `Keypad` (Mark Stanley, Alexander Brevig) from the Library Manager. `Servo`, `EEPROM` and `Wire` come with the IDE.

## Wiring

| Part | Connection |
|---|---|
| I2C LCD | SDA → A4, SCL → A5, VCC → 5V, GND → GND |
| Ultrasonic HC-SR04 | Trig → D10, Echo → D11, VCC → 5V, GND → GND |
| Buzzer | + → D12, − → GND |
| Servo | Signal → A3, VCC → 5V, GND → GND |
| Push button | One side → A2, other side → GND |
| Green LED | Long leg → A0 through a 220 Ω resistor, short leg → GND |
| Red LED | Long leg → A1 through a 220 Ω resistor, short leg → GND |
| 4×4 keypad | Rows → D9, D8, D7, D6. Columns → D5, D4, D3, D2 |

Keypad ribbon, in the order that worked:

| Row 1 | Row 2 | Row 3 | Row 4 | Col 1 | Col 2 | Col 3 | Col 4 |
|---|---|---|---|---|---|---|---|
| D9 | D8 | D7 | D6 | D5 | D4 | D3 | D2 |

- The keypad has no power pins. Don't connect it to 5V or GND.
- Every part shares the Uno's GND.
- The button uses `INPUT_PULLUP`, so the other side goes to GND, not 5V.
- Each LED needs its own resistor.
- If the board resets when the servo moves, add a 470–1000 µF capacitor across the servo's power, or give the servo its own 5V supply (with GNDs joined).

## How to use it

| Code / key | What it does |
|---|---|
| `1234` + `#` | Arm / disarm (changeable) |
| `2468` + `#` | Alert code: during the siren, opens the false alarm / real threat choice |
| `*` | Clear what you typed |
| `A` | While disarmed: change the code (old → new → repeat, `#` after each) |
| `1` / `2` | After the alert code: false alarm / real threat |
| Button | Arms when disarmed. During the alarm, press it and then type the arm code to reset |
| Button held at power-on | Resets the code to `1234` |

> ⚠️ Change both codes before using this anywhere real: they're published here.

**LEDs:** green = armed, blinking red = entry countdown, solid red = alarm, both off = disarmed.

## Files

| File | What it is |
|---|---|
| [`security_system/security_system.ino`](security_system/security_system.ino) | The main sketch |
| [`self_test/self_test.ino`](self_test/self_test.ino) | Hardware self-test: I2C scan, LCD, LEDs, buzzer, servo, then a live keypad/button/sensor test |
| [`index.html`](index.html) | The project page (GitHub Pages) |

## Troubleshooting

Upload `self_test.ino` first and open the Serial Monitor at 9600 baud. It checks every part.

| Problem | Fix |
|---|---|
| LCD lit but no text | Turn the contrast potentiometer on the back of the I2C board |
| LCD blank / blocks | Run the self-test I2C scan. If it says `0x3F`, change `lcd(0x27, …)` to `lcd(0x3F, …)` |
| Wrong keys | Ribbon reversed: swap to `rowPins = {6,7,8,9}`, `colPins = {2,3,4,5}` |
| A row/column of keys dead | That row's or column's wire is loose |
| Alarm goes off by itself | Aim the sensor away from walls, and raise `READINGS_TO_TRIGGER` |
| Sensor never triggers | Wait out the 5 s exit delay. Check Trig/Echo aren't swapped |
| Buzzer makes one sound only | It's an active buzzer. Use a passive one |
| Board resets when the servo moves | Add a capacitor or a separate servo supply |
| Forgot the code | Hold the button while plugging in |

The project page has a longer, clickable troubleshooter.

## Settings

All at the top of the sketch: `TRIGGER_DISTANCE_CM`, `READINGS_TO_TRIGGER`, `EXIT_DELAY`, `ENTRY_DELAY`, `ENTRY_TIMEOUT`, `MAX_ATTEMPTS`, `LOCKOUT_TIMES`, `MIN_CODE_LEN` / `MAX_CODE_LEN`, `HIDE_CODE`, `DEFAULT_ARM_PASSWORD`, `ALERT_PASSWORD`.

## Limitations

- "Calling 911..." is only a message. The Uno can't make calls.
- This is a learning project, not a real alarm. Unplugging it turns it off.
- Codes are stored in plain text in EEPROM.

## Publishing the project page

1. Push this folder to a GitHub repository named `arduino-security-system`.
2. In the repo, go to **Settings → Pages**, set **Source** to *Deploy from a branch*, then pick `main` and `/ (root)`.
3. After a minute the page is live at `https://akshajtripathi24-ui.github.io/arduino-security-system/`. Put that link at the top of this README.
