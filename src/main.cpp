// Timer setup
#include "esp32-hal-timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"


volatile int InterruptCounter = 0;

volatile bool pumping = false;
static int stepneeded = 0;
hw_timer_t *timer = NULL;
portMUX_TYPE timerMUX = portMUX_INITIALIZER_UNLOCKED;
volatile bool processDone = false;
volatile int totalSteps = 0;
bool updateProgressFlag = false;
volatile int completedSteps = 0;
// Flag setup
int menu_index = 0;

int volume_input_count = 0;
int speed_input_count = 0;
int diameter_input_count = 0;
static int volume_value = 5, speed_value = 10, diameter = 20;
static unsigned int syringe_type=0;


// Menu setup
bool edit_Volume = false;
bool edit_Speed = false;
bool edit_Diameter = false;
bool entered_Diameter_Menu = false;
bool entered_Volume_Menu = false;
bool entered_Speed_Menu = false;
const int numMenuItems = 3;
int current_D_Index = 0;
void volumeSettingMenu();
static int current_Volume_digit = 0;
void speedSettingMenu();
static int current_Speed_digit = 0;

typedef struct {
  const char* name_;
  int value;
  const char* suffix;
} menu_item;

menu_item menu[4];

void update_Diameter_LCD();

// Motor setup
#define STEP_PER_REV        200
#define MICROSTEP_PER_STEP  8
#define MICROSTEP_PER_REV   (STEP_PER_REV * MICROSTEP_PER_STEP)
#define SCREW_PITCH_MM      8
#define DIR_PIN 4
#define STEP_PIN 5
#define EN_PIN 16
volatile bool stepState = false;

// LCD and Buttons
#include <Wire.h>
#include <LCD_I2C.h>
LCD_I2C lcd(0x27, 16, 2);
byte customChar[] = {
  B00000,
  B00000,
  B11111,
  B11111,
  B11111,
  B11111,
  B00000,
  B00000
};

#define UP_BUTTON_PIN 8
#define DOWN_BUTTON_PIN 17
#define SELECT_BUTTON_PIN 18
bool up_flag = false;
bool down_flag = false;
bool select_flag = false;
bool down_pressed = false;
bool select_pressed = false;
bool up_pressed = false;
unsigned long previousMillis = 0; // Tracks the last time the LCD was updated
const unsigned long refreshInterval = 500; // Refresh interval in milliseconds

void update_lcd(int i) {
  lcd.clear();

  if (i == 0){
    lcd.setCursor(5, 0); 
    lcd.print(menu[i].name_);
    if(volume_value == 0 || speed_value == 0 || diameter == 0){
      lcd.setCursor(0,0);
      lcd.print("<");
      lcd.setCursor(16,0);
      lcd.print(">");
    }
  }
  if (i == 1) {
    lcd.setCursor(2, 0);
    lcd.print(menu[i].name_);
    lcd.setCursor(0,0);
    lcd.print("<");
    lcd.setCursor(16,0);
    lcd.print(">");
    lcd.setCursor(6, 1);
    lcd.print(volume_value);
    lcd.print(menu[1].suffix);
  }
  if (i == 2) {
    lcd.setCursor(4, 0);
    lcd.print(menu[i].name_);
    lcd.setCursor(0,0);
    lcd.print("<");
    lcd.setCursor(16,0);
    lcd.print(">");
    lcd.setCursor(5, 1);
    lcd.print(speed_value);
    lcd.print(menu[2].suffix);
  }
  if (i == 3) {
    lcd.setCursor(2, 0);
    lcd.print(menu[i].name_);
    lcd.setCursor(0,0);
    lcd.print("<");
    lcd.setCursor(16,0);
    lcd.print(">");
    lcd.setCursor(6, 1);
    lcd.print(syringe_type);
    lcd.print(menu[3].suffix);
  }
}
void IRAM_ATTR onTimer() {
  // Code to make the motor step
  portENTER_CRITICAL_ISR(&timerMUX);
  // statement
  digitalWrite(DIR_PIN, HIGH);
  if (InterruptCounter < stepneeded) {
    stepState = !stepState;
    digitalWrite(STEP_PIN, stepState ? HIGH : LOW);
    InterruptCounter++;
    completedSteps = InterruptCounter;
    updateProgressFlag = true;
  } else {
    processDone = true;
    pumping = false;
    digitalWrite(DIR_PIN, LOW);
  }
  portEXIT_CRITICAL_ISR(&timerMUX);
}
void updateProgressBar(int completedSteps, int totalSteps) {
  // Calculate the percentage of completion
  int percentage = (completedSteps * 100) / totalSteps;

  // Calculate the width of the progress bar
  int barWidth = 16;  // For a 16-character wide progress bar
  int progressLength = (percentage * barWidth) / 100;

  // Update LCD
  lcd.clear();
  lcd.setCursor(6, 0);
  lcd.print(percentage);
  lcd.print("%");
  lcd.setCursor(0, 1);
  Serial.println(percentage);
  // Draw the progress bar (fill with '=' for progress)
  for (int i = 0; i < progressLength; i++) {
    lcd.write(0);
  }

  // Fill the remaining space with spaces
  for (int i = progressLength; i < barWidth; i++) {
    lcd.print(" ");
  }
  delay(200);
}
float Step_Needed(int volume) {
  float volumePerStep = (SCREW_PITCH_MM  * (diameter / 2) * (diameter / 2) * 3.14) /  (MICROSTEP_PER_REV * 1000.0);
  float StepNeeded = volume_value / volumePerStep;
  Serial.print("Step_needed: ");
  Serial.println(StepNeeded);
  return 2*StepNeeded;
}

void resetSystem() {
  int step = Step_Needed(volume_value) / 2; 
  
  for (int i = 0; i < step; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(250); 
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(250); 
  }

  InterruptCounter = 0;

  pumping = false;
  stepneeded = 0;
  menu_index = 0;
  volume_input_count = 0;
  speed_input_count = 0;
  diameter_input_count = 0;
  volume_value = 0, speed_value = 0, diameter = 0;
  edit_Volume = false;
  edit_Speed = false;
  edit_Diameter = false;
  processDone = false;
  completedSteps = 0;
  InterruptCounter = 0;
  timerAlarmDisable(timer);
}

int timeperInterrrupt(float diameter, int speed) {
  float radius = diameter / 2;
  float step_per_ml = (MICROSTEP_PER_REV / (SCREW_PITCH_MM * 3.14 * radius * radius))*1000;
  Serial.print("Radius: ");
  Serial.println(radius);
  Serial.print("Step/ml: ");
  Serial.println(step_per_ml);
  float step_per_second = (speed * step_per_ml)/60;
  float microsecond_per_step = 1000000 / step_per_second;
  Serial.print("Microsecond/step: ");
  Serial.println(microsecond_per_step);
  return microsecond_per_step/2;
}

void setup() {
  Serial.begin(9600);
  lcd.createChar(0, customChar);
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SELECT_BUTTON_PIN, INPUT_PULLUP);
  
  timer = timerBegin(0, 80, true); 
  timerAttachInterrupt(timer, &onTimer, true);


  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  Wire.begin(10, 9);
  lcd.begin(); 
  lcd.backlight();

  menu[0].name_ = "Start";

  menu[1].name_ = "Volume limit";
  menu[1].suffix = "mL";
  
  menu[2].name_ = "Flow rate";
  menu[2].suffix = "mL/min";
  
  menu[3].name_ = "Syringe type";
  menu[3].suffix = "cc";

  update_lcd(menu_index);
  digitalWrite(EN_PIN, LOW);
}

void loop() {
  int timeperInterrupt;
  
  // ---------------- BUTTON HANDLING -----------------------
  int down_state = digitalRead(DOWN_BUTTON_PIN);
  int select_state = digitalRead(SELECT_BUTTON_PIN);
  int up_state = digitalRead(UP_BUTTON_PIN);
  //----DOWN BUTTON----
  if (down_state == LOW && !down_flag ) {
    down_flag = true;
    delay(50);
  }
  if (down_state == HIGH && down_flag ) {
    down_flag = false;
    down_pressed = true;
    delay(50);
  }

  if (down_pressed && !edit_Volume && !edit_Speed && !edit_Diameter) {
    menu_index--;  
    if (menu_index < 0) { menu_index = 3; }
      update_lcd(menu_index);
      Serial.println("DOWN PRESSED");
      down_pressed = false;
  }
  //---- UP BUTTON ----
  if (up_state == LOW && !up_flag ) {
    up_flag = true;
    delay(50);
  }
  if (up_state == HIGH && up_flag ) {
    up_flag = false;
    up_pressed = true;
    delay(50);
  }

  if (up_pressed && !edit_Volume && !edit_Speed && !edit_Diameter) {
    menu_index++; 
    if (menu_index > 3) { menu_index = 0; }  
    update_lcd(menu_index);
    Serial.println("UP PRESSED");
    up_pressed = false;
  }

  //----- SELECT BUTTON -------
  if (select_state == LOW && !select_flag ) {
    select_flag = true;
    delay(50);
  }
  if (select_state == HIGH && select_flag ) {
    select_flag = false;
    select_pressed = true;
    delay(50);
  }

  if (select_pressed && !edit_Volume && !edit_Speed && !edit_Diameter && !processDone) {
    select_pressed = false;
    Serial.println(select_pressed);
    switch (menu_index) {
      case 0:
        if (volume_value == 0 || speed_value == 0 || diameter == 0){
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Error setting");
          lcd.setCursor(0,1);
          lcd.print("Please check!");
          delay(2000);
          update_lcd(menu_index);
        }
        else {
        // Start the pumping process
        Serial.println("Pressed");
        pumping = true;
        update_lcd(menu_index);
        stepneeded = Step_Needed(volume_value);

        timeperInterrupt = timeperInterrrupt(diameter, speed_value);

        timerAlarmWrite(timer, timeperInterrupt, true);
        timerAlarmEnable(timer);

        Serial.println(InterruptCounter);
        }
        break;
      case 1:
        // Enter Volume Setup
        edit_Volume = true;
        entered_Volume_Menu = true;
        break;
      case 2:
        // Enter Speed Setup
        edit_Speed = true;
        entered_Speed_Menu = true;
        break;
      case 3:
        // Enter Diameter Setup
        edit_Diameter = true;
        entered_Diameter_Menu = true;
        break;
      default:
        break;
    }
    delay(50);  // Debounce delay
  }

  if (edit_Volume) {
    volumeSettingMenu();
    Serial.print("current_Volume_digit: ");
    Serial.println(current_Volume_digit);
    if (current_Volume_digit > 1 )  {
      edit_Volume = false;  // Finish editing speed
      current_Volume_digit = 0;
      update_lcd(menu_index);
      delay(100);  // Debounce delay
    }
  }

  if (edit_Speed) {
    speedSettingMenu(); // Example max and min values for speed
    Serial.print("current_Speed_digit: ");
    Serial.println(current_Speed_digit);
    if (current_Speed_digit > 1) {
      edit_Speed = false;  // Finish editing speed
      current_Speed_digit = 0;
      update_lcd(menu_index);
      delay(100);  // Debounce delay
    }
  }

  if (edit_Diameter) {
    if (entered_Diameter_Menu) {
      update_Diameter_LCD();
    }
    if (select_state == LOW && !select_flag ) {
      select_flag = true;
      delay(50);
   }
    if (select_state == HIGH && select_flag ) {
      select_flag = false;
      select_pressed = true;
      delay(50);
    }
    if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
      edit_Diameter = false;  // Finish editing diameter
      update_lcd(menu_index);
      entered_Diameter_Menu = false;
      delay(100); 
    }
  }
  if (updateProgressFlag) {
    updateProgressBar(completedSteps, stepneeded);
    updateProgressFlag = false; // Reset the flag
  }
  if (processDone) {
    lcd.clear();
    lcd.setCursor(7, 0);
    lcd.print("Done");
    lcd.setCursor(2,1);
    lcd.print("Reset system?");
    if (digitalRead(SELECT_BUTTON_PIN) == LOW){
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Resetting");
      delay(500);
      lcd.print(".");
      delay(500);
      lcd.print(".");
      delay(500);
      lcd.print(".");
      

      resetSystem();
      Serial.println("Resetting system...");
      Serial.print("DIR_PIN state: ");
      Serial.println(digitalRead(DIR_PIN));
      delay(500);
      lcd.print(".");
      delay(500);
      lcd.print(".");


      delay(1000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("System reset ");
      lcd.setCursor(0, 1);
      lcd.print("was successful.");
      delay(2000);    
      update_lcd(menu_index);
    }
    delay(200);
  }
}

// ------------------ DIAMETER MENU --------------------
void update_Diameter_LCD() {
  
  if (digitalRead(DOWN_BUTTON_PIN) == LOW && !down_flag ) {
    down_flag = true;
    delay(50);
  }
  if (digitalRead(DOWN_BUTTON_PIN) == HIGH && down_flag ) {
    down_flag = false;
    down_pressed = true;
    delay(50);
  }  

  if (down_pressed) {
    down_pressed = false;  
    current_D_Index--;  // Increment menu index
    if (current_D_Index > 2) { current_D_Index = 2; }  // Stay at last item
  }

  if (digitalRead(UP_BUTTON_PIN) == LOW && !up_flag ) {
    up_flag = true;
    delay(50);
  }
  if (digitalRead(UP_BUTTON_PIN) == HIGH && up_flag ) {
    up_flag = false;
    up_pressed = true;
    delay(50);
  }
  if (up_pressed) { 
    up_pressed = false; 
    current_D_Index++;  // Decrement menu index
    if (current_D_Index < 0) { current_D_Index = 0; }  // Stay at first item
  }

  switch (current_D_Index) {
    case 0:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(">10ml");
      lcd.setCursor(0, 1);
      lcd.print(" 20ml");
      diameter = 10;
      syringe_type = 10;
      break;
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(" 10ml");
      lcd.setCursor(0, 1);
      lcd.print(">20ml");
      diameter = 19;
      syringe_type = 20;
      break;
    case 2:
      lcd.clear();    
      lcd.setCursor(0, 0);
      lcd.print(" 20ml");
      lcd.setCursor(0, 1);
      lcd.print(">50ml");
      diameter = 50;
      syringe_type = 50;
      break;
    default:
      break;  
  }
  delay(200);
}

void volumeSettingMenu() {
    // 0 for tens, 1 for units
  static unsigned long lastBlinkTime = 0;
  static bool blinkState = true; // For blinking the pointer
  static int lastVolumeValue = -1; // To track changes in volume
  static int lastSpeedPosition = -1; // To track changes in the cursor position
  int digit_positions[2] = {0, 1}; // Cursor positions for tens and units
  
  // Blinking pointer
  if (millis() - lastBlinkTime > 200) { // 200ms
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }
  // Format the volume value as two digits
  int tens = volume_value / 10;  // Extract tens place
  int units = volume_value % 10; // Extract units place
  // Update LCD display
  if (volume_value != lastVolumeValue || current_Volume_digit != lastSpeedPosition) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Volume: ");
    lcd.print(tens); // Display tens digit
    lcd.print(units); // Display units digit
    lcd.print(" mL");
    lastVolumeValue = volume_value;
    lastSpeedPosition = current_Volume_digit;
  }
  //Calculate the pointer position
  int pointer_position = current_Volume_digit + 8;
  lcd.setCursor(pointer_position, 1);
  lcd.print(blinkState ? "^" : " "); // Blink the pointer

  // Button handling
  if (digitalRead(UP_BUTTON_PIN) == LOW && !up_flag ) {
    up_flag = true;
    delay(50);
  }
  if (digitalRead(UP_BUTTON_PIN) == HIGH && up_flag ) {
    up_flag = false;
    up_pressed = true;
    delay(50);
  }
  if (up_pressed) {
    up_pressed = false;
    if (current_Volume_digit == 0) {
      volume_value += 10; // Adjust tens place
      if (volume_value > 100) volume_value = 100; // Limit to 99
    } else {
      volume_value += 1; // Adjust units place
      if (volume_value > 100) volume_value = 100; // Prevent overflow
    }
  }

  if (digitalRead(DOWN_BUTTON_PIN) == LOW && !down_flag ) {
    down_flag = true;
    delay(50);
  }
  if (digitalRead(DOWN_BUTTON_PIN) == HIGH && down_flag ) {
    down_flag = false;
    down_pressed = true;
    delay(50);
  }
  if (down_pressed) {
    down_pressed = false;
    if (current_Volume_digit == 0) {
      volume_value -= 10; // Adjust tens place
      if (volume_value < 0) volume_value = 0; // Prevent underflow
    } else {
      volume_value -= 1; // Adjust units place
      if (volume_value % 10 == -1) volume_value += 10; // Prevent underflow
    }
  }
  if (digitalRead(SELECT_BUTTON_PIN) == LOW && !select_flag ) {
    select_flag = true;
    delay(50);
  }
  if (digitalRead(SELECT_BUTTON_PIN) == HIGH && select_flag ) {
    select_flag = false;
    select_pressed = true;
    delay(50);
  }
  if (select_pressed) {
    select_pressed = false;
    current_Volume_digit++;
  }
}
void speedSettingMenu() { 
  static unsigned long lastBlinkTime = 0;
  static bool blinkState = true;
  static int lastSpeedValue = -1; // To track changes in volume
  static int lastSpeedPosition = -1;
  int digit_positions[2] = {0, 1}; // Cursor positions for tens and units

  // Blinking pointer logic
  if (millis() - lastBlinkTime > 200) { // 200ms interval
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  // Format the speed value as two digits
  int tens = speed_value / 10;  // Extract tens place
  int units = speed_value % 10; // Extract units place

  // Update LCD display
  if (speed_value != lastSpeedValue || current_Speed_digit != lastSpeedPosition) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Speed: ");
    lcd.print(tens); // Display tens digit
    lcd.print(units); // Display units digit
    lcd.print(" mL/min");
    lastSpeedValue = speed_value;
    lastSpeedPosition = current_Speed_digit;
  }
  // Calculate the pointer position
  int pointer_position = current_Speed_digit + 7; // Adjust based on "Speed: " length
  lcd.setCursor(pointer_position, 1);
  lcd.print(blinkState ? "^" : " "); // Blink the pointer

  // Button handling
  if (digitalRead(UP_BUTTON_PIN) == LOW && !up_flag ) {
    up_flag = true;
    delay(50);
  }
  if (digitalRead(UP_BUTTON_PIN) == HIGH && up_flag ) {
    up_flag = false;
    up_pressed = true;
    delay(50);
  }

  if (up_pressed) {
    up_pressed = false;
    if (current_Speed_digit == 0) {
      speed_value += 10; // Adjust tens place
      if (speed_value > 100) speed_value = 100; // Limit to 99
    } else {
      speed_value += 1; // Adjust units place
      if (speed_value > 100) speed_value = 100; // Prevent overflow
    }
  }

  if (digitalRead(DOWN_BUTTON_PIN) == LOW && !down_flag ) {
    down_flag = true;
    delay(50);
  }
  if (digitalRead(DOWN_BUTTON_PIN) == HIGH && down_flag ) {
    down_flag = false;
    down_pressed = true;
    delay(50);
  }

  if (down_pressed) {
    down_pressed = false;
    if (current_Speed_digit == 0) {
      speed_value -= 10; // Adjust tens place
      if (speed_value < 0) speed_value = 0; // Prevent underflow
    } else {
      speed_value -= 1; // Adjust units place
      if (speed_value % 10 == -1) speed_value += 10; // Prevent underflow
    }
  }

  if (digitalRead(SELECT_BUTTON_PIN) == LOW && !select_flag ) {
    select_flag = true;
    delay(50);
  }
  if (digitalRead(SELECT_BUTTON_PIN) == HIGH && select_flag ) {
    select_flag = false;
    select_pressed = true;
    delay(50);
  }

  if (select_pressed) {
    select_pressed = false;
    current_Speed_digit++; 
  }
}