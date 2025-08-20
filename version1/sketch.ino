#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>

// OLED Configuration
#define OLED_SDA 22
#define OLED_SCL 21
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_ADDR);

// Button Configuration (Active LOW)
#define BTN_UP 34
#define BTN_LEFT 26
#define BTN_DOWN 32
#define BTN_RIGHT 35
#define BUZZER_PIN 2
#define LED_PIN 18

// DHT22 Sensor Configuration
#define DHTPIN 23
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Environmental Limits
#define MIN_TEMP 24
#define MAX_TEMP 32
#define MIN_HUMIDITY 65
#define MAX_HUMIDITY 80

// Timing Constants
#define DEBOUNCE_TIME 200
#define ENV_CHECK_INTERVAL 2000
#define LED_TOGGLE_INTERVAL 500
#define SNOOZE_DURATION 120000

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, 0, 60000);

// Timezone
int timeZoneOffset = 19800; // Default: Sri Lanka (UTC+5:30)

// WiFi Configuration
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Alarm System
struct Alarm {
  int hour;
  int minute;
  bool active;
  bool ringing;
  bool snoozed;
  unsigned long snoozeStartTime;
};
Alarm alarms[2] = {{0, 0, false, false, false, 0}, {0, 0, false, false, false, 0}};

// System State
bool alarmTriggered = false;
bool envWarning = false;
int currentAlarmIndex = 0;
int viewAlarmsSelection = 0;
float temperature = 0;
float humidity = 0;
unsigned long lastEnvCheck = 0;
unsigned long lastLedToggle = 0;

// Menu System
enum AppState { WELCOME, SHOW_TIME, MAIN_MENU, SET_ALARM_HOUR, SET_ALARM_MINUTE, 
               SET_TIMEZONE, VIEW_ALARMS, ALARM_TRIGGERED, CONFIRM_DELETE };
AppState currentState = WELCOME;
int menuOption = 0;

const char* menuItems[] = {
  "Set Alarm 1",
  "Set Alarm 2",
  "Set Timezone",
  "View Alarms"
};
const int menuItemCount = 4;

// Button interrupt flags
volatile bool btnUpPressed = false;
volatile bool btnLeftPressed = false;
volatile bool btnDownPressed = false;
volatile bool btnRightPressed = false;
volatile bool stopAlarmFlag = false;
volatile bool snoozeAlarmFlag = false;
unsigned long lastInterruptTime = 0;

// Function Declarations
void IRAM_ATTR handleUpInterrupt();
void IRAM_ATTR handleLeftInterrupt();
void IRAM_ATTR handleDownInterrupt();
void IRAM_ATTR handleRightInterrupt();
void connectToWiFi();
void updateTimeZone();
void displayWelcome();
void displayTime();
void displayMenu();
void displaySetAlarmHour();
void displaySetAlarmMinute();
void displaySetTimezone();
void displayViewAlarms();
void displayConfirmDelete();
void checkEnvironment();
void handleLED();
void checkAlarms();
void handleAlarmTrigger();
void stopAlarm();
void snoozeAlarm();
String getDayOfWeek(int day);
void handleButtons();
void handleRightButton();
void handleLeftButton();
void handleUpButton();
void handleDownButton();
void updateDisplay();

// Interrupt Service Routines
void IRAM_ATTR handleUpInterrupt() { 
  if (millis() - lastInterruptTime > DEBOUNCE_TIME) { 
    btnUpPressed = true; 
    lastInterruptTime = millis(); 
  } 
}

void IRAM_ATTR handleLeftInterrupt() { 
  if (millis() - lastInterruptTime > DEBOUNCE_TIME) { 
    btnLeftPressed = true; 
    lastInterruptTime = millis(); 
  } 
}

void IRAM_ATTR handleDownInterrupt() { 
  if (millis() - lastInterruptTime > DEBOUNCE_TIME) { 
    btnDownPressed = true; 
    if (currentState == ALARM_TRIGGERED) {
      snoozeAlarmFlag = true;
    }
    lastInterruptTime = millis(); 
  } 
}

void IRAM_ATTR handleRightInterrupt() { 
  if (millis() - lastInterruptTime > DEBOUNCE_TIME) { 
    btnRightPressed = true; 
    if (currentState == ALARM_TRIGGERED) {
      stopAlarmFlag = true;
    }
    lastInterruptTime = millis(); 
  } 
}

void setup() {
  Serial.begin(115200);
  
  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED allocation failed");
    while(1);
  }
  
  // Initialize Buttons with interrupts
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  attachInterrupt(digitalPinToInterrupt(BTN_UP), handleUpInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), handleLeftInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_DOWN), handleDownInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT), handleRightInterrupt, FALLING);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Show welcome message
  displayWelcome();
  delay(2000);
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize NTP client
  timeClient.begin();
  updateTimeZone();
  
  // Turn LED on initially
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  timeClient.update();
  handleButtons();
  checkEnvironment();
  updateDisplay();
  checkAlarms();
  handleLED();
  
  if (alarmTriggered) {
    handleAlarmTrigger();
  }
  
  // Check for immediate alarm actions
  if (stopAlarmFlag) {
    stopAlarmFlag = false;
    stopAlarm();
  }
  
  if (snoozeAlarmFlag) {
    snoozeAlarmFlag = false;
    snoozeAlarm();
  }
  
  delay(100);
}

void connectToWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connecting to WiFi");
  display.println(ssid);
  display.display();
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    display.print(".");
    display.display();
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Failed!");
    display.println("Retrying...");
    display.display();
    delay(2000);
    connectToWiFi();
    return;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(1000);
}


void updateTimeZone() {
  timeClient.setTimeOffset(timeZoneOffset);
}

void displayWelcome() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 10);
  display.println("  MEDIBOX");
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.println("Press RIGHT to begin");
  display.display();
}

void checkEnvironment() {
  if (millis() - lastEnvCheck > ENV_CHECK_INTERVAL) {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    lastEnvCheck = millis();
    
    // Check if values are within limits
    envWarning = (temperature < MIN_TEMP || temperature > MAX_TEMP || 
                 humidity < MIN_HUMIDITY || humidity > MAX_HUMIDITY);
    
    // Activate buzzer if limits exceeded (unless alarm is ringing)
    if (envWarning && !alarmTriggered) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

void handleLED() {
  if (envWarning) {
    // Blink LED if warning
    if (millis() - lastLedToggle > LED_TOGGLE_INTERVAL) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastLedToggle = millis();
    }
  } else {
    // Keep LED on if no warning
    digitalWrite(LED_PIN, HIGH);
  }
}

void displayTime() {
  display.clearDisplay();
  
  // Display time (large)
  display.setTextSize(2);
  display.setCursor(0, 10);
  String formattedTime = timeClient.getFormattedTime();
  display.println(formattedTime);
  
  // Display date (medium)
  display.setTextSize(1);
  display.setCursor(0, 35);
  
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime(&epochTime);
  
  char dateString[30];
  sprintf(dateString, "%02d/%02d/%04d %s", 
         ptm->tm_mday, ptm->tm_mon+1, ptm->tm_year+1900,
         getDayOfWeek(ptm->tm_wday).c_str());
  display.println(dateString);
  
  // Display environmental data
  display.setCursor(0, 50);
  display.print(temperature, 1);
  display.print("C ");
  display.print(humidity, 0);
  display.print("%");
  
  // Show warning indicator if needed
  if (envWarning) {
    display.setCursor(110, 50);
    display.print("!");
    
    // Display warning message
    display.setCursor(0, 0);
    if (temperature < MIN_TEMP) display.print("LOW TEMP! ");
    if (temperature > MAX_TEMP) display.print("HIGH TEMP! ");
    if (humidity < MIN_HUMIDITY) display.print("LOW HUM! ");
    if (humidity > MAX_HUMIDITY) display.print("HIGH HUM! ");
  }
  
  // Display alarm indicators
  for (int i = 0; i < 2; i++) {
    if (alarms[i].active) {
      display.setCursor(110, i * 10);
      display.print("A");
      display.print(i+1);
    }
  }
}

void checkAlarms() {
  if (alarmTriggered) return;
  
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  unsigned long currentMillis = millis();
  
  for (int i = 0; i < 2; i++) {
    // Handle snoozed alarms
    if (alarms[i].snoozed) {
      if (currentMillis - alarms[i].snoozeStartTime >= SNOOZE_DURATION) {
        alarms[i].snoozed = false;
        // Reactivate the alarm for the next minute
        alarms[i].hour = currentHour;
        alarms[i].minute = currentMinute;
      } else {
        continue; // Skip if still in snooze period
      }
    }
    
    if (alarms[i].active && !alarms[i].ringing &&
        alarms[i].hour == currentHour && 
        alarms[i].minute == currentMinute) {
      alarms[i].ringing = true;
      alarmTriggered = true;
      currentState = ALARM_TRIGGERED;
    }
  }
}

void handleAlarmTrigger() {
  static unsigned long lastBeep = 0;
  static bool buzzerState = false;
  
  // Override environmental warning buzzer
  digitalWrite(BUZZER_PIN, LOW);
  
  // Buzzer beeping (500ms on, 500ms off)
  if (millis() - lastBeep > 500) {
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    lastBeep = millis();
  }
  
  // Display alarm message with current time
  display.clearDisplay();
  
  // Show current time at top
  display.setTextSize(1);
  display.setCursor(0, 0);
  String formattedTime = timeClient.getFormattedTime();
  display.println(formattedTime);
  
  // Show environmental data
  display.setCursor(70, 0);
  display.print(temperature, 1);
  display.print("C ");
  display.print(humidity, 0);
  display.print("%");
  
  // Show alarm message
  display.setTextSize(2);
  display.setCursor(0, 15);
  
  // Find which alarm is triggered
  for (int i = 0; i < 2; i++) {
    if (alarms[i].ringing) {
      display.print("ALARM ");
      display.println(i+1);
      currentAlarmIndex = i;
      break;
    }
  }
  
  display.setTextSize(1);
  display.setCursor(0, 35);
  display.println("RIGHT: Stop Alarm");
  display.setCursor(0, 45);
  display.println("DOWN: Snooze (2min)");
  
  display.display();
}

void stopAlarm() {
  digitalWrite(BUZZER_PIN, LOW);
  alarmTriggered = false;
  
  // Turn off all ringing alarms
  for (int i = 0; i < 2; i++) {
    alarms[i].ringing = false;
    alarms[i].snoozed = false;
  }
  
  currentState = SHOW_TIME;
}

void snoozeAlarm() {
  digitalWrite(BUZZER_PIN, LOW);
  alarmTriggered = false;
  
  // Set snooze state
  alarms[currentAlarmIndex].ringing = false;
  alarms[currentAlarmIndex].snoozed = true;
  alarms[currentAlarmIndex].snoozeStartTime = millis();
  
  currentState = SHOW_TIME;
}

String getDayOfWeek(int day) {
  switch(day) {
    case 0: return "Sun";
    case 1: return "Mon";
    case 2: return "Tue";
    case 3: return "Wed";
    case 4: return "Thu";
    case 5: return "Fri";
    case 6: return "Sat";
    default: return "";
  }
}

void handleButtons() {
  if (btnRightPressed) {
    btnRightPressed = false;
    handleRightButton();
  }
  if (btnLeftPressed) {
    btnLeftPressed = false;
    handleLeftButton();
  }
  if (btnUpPressed) {
    btnUpPressed = false;
    handleUpButton();
  }
  if (btnDownPressed) {
    btnDownPressed = false;
    handleDownButton();
  }
}

void handleRightButton() {
  switch(currentState) {
    case WELCOME:
      currentState = SHOW_TIME;
      break;
    case SHOW_TIME:
      currentState = MAIN_MENU;
      menuOption = 0;
      break;
    case MAIN_MENU:
      if (menuOption == 0) {
        currentAlarmIndex = 0;
        currentState = SET_ALARM_HOUR;
      } else if (menuOption == 1) {
        currentAlarmIndex = 1;
        currentState = SET_ALARM_HOUR;
      } else if (menuOption == 2) {
        currentState = SET_TIMEZONE;
      } else if (menuOption == 3) {
        viewAlarmsSelection = 0;
        currentState = VIEW_ALARMS;
      }
      break;
    case SET_ALARM_HOUR:
      currentState = SET_ALARM_MINUTE;
      break;
    case SET_ALARM_MINUTE:
      alarms[currentAlarmIndex].active = true;
      currentState = MAIN_MENU;
      break;
    case SET_TIMEZONE:
      currentState = MAIN_MENU;
      break;
    case VIEW_ALARMS:
      if (alarms[viewAlarmsSelection].active) {
        currentState = CONFIRM_DELETE;
      }
      break;
    case CONFIRM_DELETE:
      alarms[viewAlarmsSelection].active = false;
      alarms[viewAlarmsSelection].ringing = false;
      alarms[viewAlarmsSelection].snoozed = false;
      currentState = VIEW_ALARMS;
      break;
    case ALARM_TRIGGERED:
      stopAlarm();
      break;
    default:
      currentState = MAIN_MENU;
  }
}

void handleLeftButton() {
  if (currentState == SHOW_TIME) {
    currentState = WELCOME;
  } else if (currentState == MAIN_MENU || currentState == VIEW_ALARMS || currentState == CONFIRM_DELETE) {
    currentState = SHOW_TIME;
  } else if (currentState == SET_ALARM_HOUR || currentState == SET_ALARM_MINUTE || currentState == SET_TIMEZONE) {
    currentState = MAIN_MENU;
  }
}

void handleUpButton() {
  switch(currentState) {
    case SET_ALARM_HOUR:
      alarms[currentAlarmIndex].hour = (alarms[currentAlarmIndex].hour + 1) % 24;
      break;
    case SET_ALARM_MINUTE:
      alarms[currentAlarmIndex].minute = (alarms[currentAlarmIndex].minute + 1) % 60;
      break;
    case SET_TIMEZONE:
      timeZoneOffset += 1800; // 30 minutes
      if (timeZoneOffset > 86400) timeZoneOffset -= 86400;
      updateTimeZone();
      break;
    case MAIN_MENU:
      menuOption = (menuOption - 1 + menuItemCount) % menuItemCount;
      break;
    case VIEW_ALARMS:
      viewAlarmsSelection = (viewAlarmsSelection - 1 + 2) % 2;
      break;
    case CONFIRM_DELETE:
      alarms[viewAlarmsSelection].active = false;
      alarms[viewAlarmsSelection].ringing = false;
      alarms[viewAlarmsSelection].snoozed = false;
      currentState = VIEW_ALARMS;
      break;
  }
}

void handleDownButton() {
  switch(currentState) {
    case SET_ALARM_HOUR:
      alarms[currentAlarmIndex].hour = (alarms[currentAlarmIndex].hour - 1 + 24) % 24;
      break;
    case SET_ALARM_MINUTE:
      alarms[currentAlarmIndex].minute = (alarms[currentAlarmIndex].minute - 1 + 60) % 60;
      break;
    case SET_TIMEZONE:
      timeZoneOffset -= 1800; // 30 minutes
      if (timeZoneOffset < -86400) timeZoneOffset += 86400;
      updateTimeZone();
      break;
    case MAIN_MENU:
      menuOption = (menuOption + 1) % menuItemCount;
      break;
    case VIEW_ALARMS:
      viewAlarmsSelection = (viewAlarmsSelection + 1) % 2;
      break;
    case CONFIRM_DELETE:
      currentState = VIEW_ALARMS;
      break;
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Handle ALARM_TRIGGERED state separately
  if (currentState == ALARM_TRIGGERED) {
    // Show current time at top
    display.setTextSize(1);
    display.setCursor(0, 0);
    String formattedTime = timeClient.getFormattedTime();
    display.println(formattedTime);
    
    // Show environmental data
    display.setCursor(70, 0);
    display.print(temperature, 1);
    display.print("C ");
    display.print(humidity, 0);
    display.print("%");
    
    // Show alarm message
    display.setTextSize(2);
    display.setCursor(0, 15);
    
    // Find which alarm is triggered
    for (int i = 0; i < 2; i++) {
      if (alarms[i].ringing) {
        display.print("ALARM ");
        display.println(i+1);
        currentAlarmIndex = i;
        break;
      }
    }
    
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println("RIGHT: Stop Alarm");
    display.setCursor(0, 45);
    display.println("DOWN: Snooze (2min)");
    
    display.display();
    return;
  }

  // Handle all other states
  switch(currentState) {
    case WELCOME:
      displayWelcome();
      break;
    case SHOW_TIME:
      displayTime();
      break;
    case MAIN_MENU:
      displayMenu();
      break;
    case SET_ALARM_HOUR:
      displaySetAlarmHour();
      break;
    case SET_ALARM_MINUTE:
      displaySetAlarmMinute();
      break;
    case SET_TIMEZONE:
      displaySetTimezone();
      break;
    case VIEW_ALARMS:
      displayViewAlarms();
      break;
    case CONFIRM_DELETE:
      displayConfirmDelete();
      break;
  }
  
  display.display();
}

void displayMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Main Menu:");
  
  // Display menu items
  for (int i = 0; i < menuItemCount; i++) {
    display.setCursor(5, 15 + (i * 12));
    if (i == menuOption) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(menuItems[i]);
  }
  
  // Display instructions
  display.setCursor(0, 55);
  display.println("LEFT:Exit RIGHT:Select");
}

void displaySetAlarmHour() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Set Alarm ");
  display.print(currentAlarmIndex + 1);
  display.println(" Hour:");
  
  display.setTextSize(2);
  display.setCursor(40, 25);
  if (alarms[currentAlarmIndex].hour < 10) display.print("0");
  display.print(alarms[currentAlarmIndex].hour);
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.println("UP/DOWN:Change RIGHT:Next");
}

void displaySetAlarmMinute() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Set Alarm ");
  display.print(currentAlarmIndex + 1);
  display.println(" Minute:");
  
  display.setTextSize(2);
  display.setCursor(40, 25);
  if (alarms[currentAlarmIndex].minute < 10) display.print("0");
  display.print(alarms[currentAlarmIndex].minute);
  
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.println("UP/DOWN:Change RIGHT:Save");
}

void displaySetTimezone() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Set Timezone Offset");
  
  display.setCursor(0, 20);
  display.print("UTC");
  if (timeZoneOffset >= 0) display.print("+");
  display.print(timeZoneOffset / 3600);
  display.print(":");
  int minutes = abs((timeZoneOffset % 3600) / 60);
  if (minutes < 10) display.print("0");
  display.print(minutes);
  
  // Convert to HMS
  int hours = timeZoneOffset / 3600;
  int mins = (abs(timeZoneOffset) % 3600) / 60;
  
  display.setCursor(0, 35);
  display.print("(");
  if (timeZoneOffset >= 0) display.print("+");
  display.print(hours);
  display.print("h ");
  if (mins < 10) display.print("0");
  display.print(mins);
  display.print("m)");
  
  // Display instructions
  display.setCursor(0, 55);
  display.println("UP/DOWN:Change RIGHT:Save");
}

void displayViewAlarms() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Active Alarms:");
  
  for (int i = 0; i < 2; i++) {
    display.setCursor(5, 15 + (i * 15));
    if (i == viewAlarmsSelection) display.print("> ");
    else display.print("  ");
    
    display.print("Alarm ");
    display.print(i+1);
    display.print(": ");
    if (alarms[i].active) {
      if (alarms[i].hour < 10) display.print("0");
      display.print(alarms[i].hour);
      display.print(":");
      if (alarms[i].minute < 10) display.print("0");
      display.print(alarms[i].minute);
    } else {
      display.print("Not set");
    }
  }
  
  // Display instructions
  display.setCursor(0, 55);
  if (alarms[viewAlarmsSelection].active) {
    display.println("LEFT:Exit RIGHT:Delete");
  } else {
    display.println("LEFT:Exit");
  }
}

void displayConfirmDelete() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Delete Alarm ");
  display.print(viewAlarmsSelection + 1);
  display.println("?");
  
  display.setCursor(0, 20);
  display.print(alarms[viewAlarmsSelection].hour < 10 ? "0" : "");
  display.print(alarms[viewAlarmsSelection].hour);
  display.print(":");
  display.print(alarms[viewAlarmsSelection].minute < 10 ? "0" : "");
  display.println(alarms[viewAlarmsSelection].minute);
  
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println("UP: Yes, DOWN: No");
}