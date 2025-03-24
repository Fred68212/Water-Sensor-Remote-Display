#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Arduino_JSON.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Fonts/FreeSans9pt7b.h> // Include the 9pt Sans-serif font
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <esp_sleep.h>
#include <secrets.h>
#include <esp_now.h>
#include <string>
#include <iostream>

// Static IP address configuration
IPAddress local_IP(192, 168, 1, 41);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);   // Optional
IPAddress secondaryDNS(0, 0, 0, 0); // Optional

// Create an instance of the web server on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

JSONVar configValue;

String confTemp = ""; // Temp string to hold the config file content

// OLED display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LED_BUILTIN 2
#define pirPin 19

volatile int minLevel = 7;
volatile int maxLevel = 10;
volatile int alertValue = 5;

int number;
int pirVal = 0;
bool pirTriggered = false;
int count;

// Alarm variables
volatile int lastAlarm = 0;
hw_timer_t *Timer0_Cfg = NULL;

// DS3231 RTC module
RTC_DS3231 rtc;

// the pin that is connected to SQW
#define CLOCK_INTERRUPT_PIN 4

const String statusServer PROGMEM = "http://192.168.1.11/status.php?unitID=Monitor&unitStatus=";

String systemStatus = "ok";

// Structure example to send data
// Must match the receiver structure
typedef struct struct_message {
  int b;
} struct_message;

// Create a struct_message called myData
struct_message myData;

bool timerFlag = false;


String readConfigFile() {
  File configFile = LittleFS.open("/config.txt", "r");
  if (!configFile) {
    Serial.println("Failed to open config file for reading");
    return "";
  } 
    // Read the file
  String confTemp = configFile.readString();
  configFile.close();
  Serial.println(confTemp);
  configValue = JSON.parse(confTemp);
  if (JSON.typeof(configValue) == "object") {
    Serial.println("Config file read successfully");

    if (configValue.hasOwnProperty("minLevel")){
        minLevel = (int)configValue["minLevel"];
    } else {
      Serial.println("minLevel key does not exist");
    }
    if (configValue.hasOwnProperty("maxLevel")){
      maxLevel = (int)configValue["maxLevel"];
    } else {
      Serial.println("maxLevel key does not exist");
    }
    if (configValue.hasOwnProperty("alertValue")){
      alertValue = (int)configValue["alertValue"];
    } else {
      Serial.println("alertValue key does not exist");
    }
    Serial.println("Config values:");
    Serial.println(minLevel);
    Serial.println(maxLevel);
    Serial.println(alertValue);
  } else {
    Serial.println("Failed to parse config file or not a JSON object");
    return "";
}
  String jsonString = JSON.stringify(configValue);
  return jsonString;
}

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Int: ");
  Serial.println(myData.b);
}


void IRAM_ATTR onAlarm() {
  
  // Set the flag
  // digitalWrite(ledPin, HIGH); // Turn on LED for troubleshooting

  if (lastAlarm == 1) {
    lastAlarm = 0;  // set alarm 1 flag to false  
  } else {
    lastAlarm = 1;  // set alarm 1 flag to true
  }
  
} 

// Variable to store the received string data
// String receivedString = "0";

String timeString = "";

void triggerFlag() {

  pirTriggered = true;
  count = 0;
  
}

  void printTwoDigits(int number) {

    if (number < 10) {
    
    Serial.print("0"); // Add a leading zero for single-digit numbers
    
    }
    
    Serial.print(number);
    
}

String formatTime(DateTime now) {
  char timeBuffer[9]; // HH:MM:SS + null terminator
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  return String(timeBuffer);
}
  
void showTime() {
  DateTime now = rtc.now();
  Serial.println("Current Time: " + formatTime(now));
}
  
  

  void dbInsert(const char* urlString) {
    HTTPClient http;
    http.begin(urlString);
    int httpCode = http.GET();
    if (httpCode > 0) { //Check for the returning code
      if (httpCode == HTTP_CODE_OK) { 
        // get payload with http.getString();
        Serial.println(httpCode);
        // Serial.println(payload);
      } else {

        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      }
    } else {
      Serial.println("Error on HTTP request");
    }
    http.end();
  }

  void writeConfigFile(String configValue) {
    Serial.println("Writing config file");
    Serial.println(configValue);

    File configFile = LittleFS.open("/config.txt", "w");

    if (!configFile) {
      Serial.println("Failed to open config file for writing");
      return;
    }
    Serial.println(configValue);
    configFile.println(configValue);
    configFile.close();
    Serial.println("Config file written successfully");
    readConfigFile();
  } 

  void notifyClients(String configValue) {
    ws.textAll(configValue);
  }
  

  String getConfiguration() {
    configValue["minLevel"] = minLevel;
    Serial.println(configValue["minLevel"]);
    configValue["maxLevel"] = maxLevel;
    Serial.println(configValue["maxLevel"]);
    configValue["alertValue"] = alertValue;
    Serial.println(configValue["alertValue"]);
    String jsonString = JSON.stringify(configValue);
    configValue = jsonString;
    return jsonString;
  }
// Function to handle incoming GET requests and store string data

void handleGet(AsyncWebServerRequest *request) {
  int tmp;
  tmp = request->getParam("currentMinLevel")->value().toInt();
  if ( tmp > 0 ) {
    minLevel = tmp;
  }
  tmp = request->getParam("currentMaxLevel")->value().toInt();
  if ( tmp > 0 ) {
    maxLevel = tmp;
  }
  tmp = request->getParam("data")->value().toInt();
  if ( tmp > 0 ) {
    alertValue = tmp;
  }
  configValue["minLevel"] = minLevel;
  configValue["maxLevel"] = maxLevel;
  configValue["alertValue"] = alertValue;
  writeConfigFile(JSON.stringify(configValue));

  

  /*
  maxLevel = request->getParam("currentMaxLevel")->value().toInt();
  alertValue = request->getParam("data")->value().toInt();
  //Serial.printlnconfigValue["minLevel"] = minLevel;
  
  if (request->hasParam("currentMinLevel") && request->hasParam("currentMaxLevel") && request->hasParam("data")) {
      String minLevelStr = request->getParam("currentMinLevel")->value();
      String maxLevelStr = request->getParam("currentMaxLevel")->value();
      String alertValueStr = request->getParam("data")->value();

    minLevel = minLevelStr.toInt();
    // Serial.print("Converted integer: ");
    // Serial.println(minLevel);
    maxLevel = maxLevelStr.toInt();
    // Serial.print("Converted integer: ");
    // Serial.println(maxLevel);
    alertValue = alertValueStr.toInt();
    // Serial.print("Converted integer: ");
    // Serial.println(alertValue);
    
    configValue["minLevel"] = minLevel;
    configValue["maxLevel"] = maxLevel;
    configValue["alertValue"] = alertValue;
    */
    Serial.println("received values:");
    Serial.println(minLevel);
    Serial.println(maxLevel);
    Serial.println(alertValue);
    Serial.println("received config:");
    Serial.println(configValue);
    


  // notify client
    notifyClients(JSON.stringify(configValue));
      
  // Respond with the index.html
    request->send(LittleFS, "/index.html", "text/html");

//  } else {
//      request->send(400, "text/plain", "Invalid request: Missing parameters");
}



// Function to update the timeString variable with the current time
void updateTime() {
  DateTime now = rtc.now();
  bool isPM = now.hour() >= 12;
  int hour = now.hour() % 12;
  if (hour == 0) hour = 12; // Convert 0 hour to 12
  char timeBuffer[6]; // HH:MM + null terminator
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", hour, now.minute());
  timeString = timeBuffer; // If you need to use timeString later, assign it here.
  // timeString = String(hour) + ":";
  // if (now.minute() < 10) {
  //  timeString = timeString + "0";
}
  // timeString = timeString + String(now.minute());


void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String configuration = getConfiguration();
      Serial.print(configuration);
      notifyClients(configuration);    
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

void setup() {
  Serial.begin(115200);

  initLittleFS();
  initWebSocket();

  // Set the static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Initialize the OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // Initialize the RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  //Uncomment the below line to set the initial date and time

 // rtc.adjust(DateTime(__DATE__, __TIME__));  

  // Set the SQW pin to generate a 1Hz signal
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);

// Show the current time in the Serial Monitor  
  showTime();

  pinMode(CLOCK_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CLOCK_INTERRUPT_PIN), onAlarm, FALLING);

/*
 set alarm 1, 2 flag to false (so alarm 1, 2 didn't happen so far)
 if not done, this easily leads to problems, as both register aren't reset on reboot/recompile
 */
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);

// stop oscillating signals at SQW Pin otherwise setAlarm1 will fail
  rtc.writeSqwPinMode(DS3231_OFF);

// Set the alarm to go off at 9:00 AM
   rtc.setAlarm1(DateTime(0, 0, 0, 8, 57, 0), DS3231_A1_Hour);
// rtc.setAlarm1(rtc.now() + TimeSpan(0, 0, 20, 0), DS3231_A1_Minute);
// printAlarm();

// Is alarm set and for when
  DateTime alarm = rtc.getAlarm1();
  Serial.print("Alarm set for: ");
  printTwoDigits(alarm.hour());
  Serial.print(":");
  printTwoDigits(alarm.minute());
  Serial.println();

// Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
/*    
    Once ESPNow is successfully Init, we will register for recv CB to
    get recv packer info
*/
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Check to see if config.txt file exists
  if (LittleFS.exists("/config.txt")) {
    Serial.println("config.txt exists");
    configValue = readConfigFile(); //Read the file directly to the configValue object

        // Print the values from the JSON document
        Serial.print("minLevel: ");
        Serial.println(configValue["minLevel"]);
        Serial.print("maxLevel: ");
        Serial.println(configValue["maxLevel"]);
        Serial.print("alertValue: ");        Serial.println(configValue["alertValue"]);
  } else {
      Serial.println("config.txt does not exist");
      configValue["minLevel"] = 7;
      configValue["maxLevel"] = 10;
      configValue["alertValue"] = 5;
      writeConfigFile(configValue);
  }

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  // Start the web server
//  server.begin();

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(pirPin, INPUT);

  server.on("/config", HTTP_GET, handleGet);

  // Print the initial time
  updateTime();

  systemStatus = "Running";
  String urlString = statusServer + systemStatus;
  Serial.println(urlString);
  dbInsert(urlString.c_str());

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  // Start server
  server.begin();

}

void loop() {

  if (rtc.now().hour() == 20 && rtc.now().minute() == 50) {

    systemStatus = "Sleeping";
    String urlString = statusServer + systemStatus;
    Serial.println(urlString);
    dbInsert(urlString.c_str());
    Serial.println("Shutting off Bluetooth and Wifi...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("Wifi off");
    Serial.flush();
    delay(1000);
    esp_err_t rtc_gpio_pullup_en(GPIO_NUM_4);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);
    esp_deep_sleep_start();
  }
 
  // Wake up ESP32 at 9:00 AM
  if (rtc.alarmFired(1)) {
    Serial.println("Waking up...");
    rtc.clearAlarm(1); // Clear the alarm flag
  }


  unsigned long lastUpdateTime = 0;
  unsigned long currentMillis = millis();
  // Update the display every minute
  if (currentMillis - lastUpdateTime >= 60000) {
    lastUpdateTime = currentMillis;
    updateTime();
  }

  pirVal = digitalRead(pirPin);

  if (pirVal == HIGH) {
    pirTriggered = true;
    currentMillis = millis();
  // Turn the LED on
  //  digitalWrite(LED_BUILTIN, HIGH);

    if (currentMillis - lastUpdateTime >= 60000) {
      lastUpdateTime = currentMillis;
      updateTime();
    }

  // Clear the display and print the received string
  display.clearDisplay();

  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 15);
  display.print("LAST:");
  display.print(myData.b);
  display.print(" IN.");
  display.drawLine(0, 20, SCREEN_WIDTH, 20, SSD1306_WHITE);

// Print the time in 24pt Sans font
  display.setFont(&FreeSansBold24pt7b);
  display.setTextSize(1);
  display.setCursor(0, 62);
  display.println(timeString);

  // Update the display
  display.display();

  if (myData.b <= alertValue) {
    delay(15000);
    display.clearDisplay();
    display.setFont(&FreeSansBold12pt7b);
    display.setTextSize(1);
    display.setCursor(10, 25);
    display.println("NEEDS");
    display.setCursor(10, 55);
    display.println("ATTN!");    
  
    // Update the display
    display.display();
    delay(15000);
  }

  } else {
    pirTriggered = false;
    digitalWrite(LED_BUILTIN, LOW);
  // Add a delay before turning off the display
  //  delay(15000);
  // Turn display off
    display.clearDisplay();
    display.display();
  }

}

