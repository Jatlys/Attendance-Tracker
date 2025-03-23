#include <SPI.h>
#include <MFRC522.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "YOUR WIFI SSID"           // Replace with your Wi-Fi SSID
#define WIFI_PASSWORD "YOUR WIFI PASSWORD"   // Replace with your Wi-Fi password

// Firebase credentials
#define FIREBASE_HOST "YOUR FIREBASE HOST URL"    // Replace with your Firebase host
#define FIREBASE_AUTH "YOUR FIREBASE API KEY"              // Replace with your Firebase secret

// RFID Module pins
#define SS_PIN 5    // SDA pin
#define SCK_PIN 4   // SCK pin
#define MOSI_PIN 6  // MOSI pin
#define MISO_PIN 7  // MISO pin
#define RST_PIN 10  // RST pin

// LED pin
#define LED_PIN 1   // LED pin

// RTC module pins
#define RTC_CLK_PIN 2   // CLK pin
#define RTC_DAT_PIN 3   // DAT pin
#define RTC_RST_PIN 8   // RST pin

// Initialize RFID
MFRC522 rfid(SS_PIN, RST_PIN);

// Initialize RTC
ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN);
RtcDS1302<ThreeWire> rtc(myWire);

// Firebase objects
FirebaseData firebaseData;         // For sending/receiving data
FirebaseJson json;                 // JSON object
FirebaseConfig config;             // Firebase configuration
FirebaseAuth auth;                 // Firebase authentication

// Wi-Fi connection status
bool wifiConnected = false;

void setup() {
  Serial.begin(115200);
  
  Serial.println("\nInitializing Attendance System...");

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize SPI for RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  
  // Initialize RFID
  rfid.PCD_Init();
  delay(50);
  Serial.print("RFID Reader: ");
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("NOT DETECTED");
    blinkError(3);
  } else {
    Serial.print("DETECTED (Version 0x");
    Serial.print(version, HEX);
    Serial.println(")");
  }

  // Initialize RTC
  rtc.Begin();
  Serial.print("RTC Module: ");

  // Check if RTC has valid time
  if (!rtc.IsDateTimeValid()) {
    Serial.println("NOT RUNNING - Setting time now");
    blinkError(5);
  } else {
    Serial.println("RUNNING");
    printDateTime(rtc.GetDateTime());
  }

  // Connect to Wi-Fi
  connectWiFi();

  // Initialize Firebase (Updated for new Firebase library)
  if (wifiConnected) {
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    Serial.println("Firebase connected!");

    // Create a test entry to verify connection
    json.set("message", "System started successfully");
    json.set("timestamp", getCurrentTimestamp());
    if (Firebase.pushJSON(firebaseData, "/system_logs", json)) {
      Serial.println("Firebase test entry successful");
    } else {
      Serial.println("Firebase test entry failed");
      Serial.println("REASON: " + firebaseData.errorReason());
    }
  }

  // Test LED
  Serial.print("Testing LED... ");
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  Serial.println("DONE");

  Serial.println("System ready. Please scan an RFID tag.");
  Serial.println("----------------------------------------");
}

void loop() {
  // Check if Wi-Fi is connected and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectWiFi();
  }

  // Check for RFID cards
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    // Get current time
    RtcDateTime now = rtc.GetDateTime();
    
    // Turn on LED
    digitalWrite(LED_PIN, HIGH);

    // Get card UID
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uid += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      uid += String(rfid.uid.uidByte[i], HEX);
      if (i < rfid.uid.size - 1) {
        uid += ":";
      }
    }
    uid.toUpperCase();

    // Format date time string
    char dateTimeStr[20];
    snprintf_P(dateTimeStr, 
               countof(dateTimeStr),
               PSTR("%04u-%02u-%02u %02u:%02u:%02u"),
               now.Year(),
               now.Month(),
               now.Day(),
               now.Hour(),
               now.Minute(),
               now.Second());

    // Print to serial
    Serial.print("Attendance for ");
    Serial.print(uid);
    Serial.print(" is clocked in at ");
    Serial.println(dateTimeStr);

    // Send to Firebase
    if (wifiConnected) {
      json.clear();
      json.set("uid", uid);
      json.set("timestamp", String(dateTimeStr));
      json.set("status", "check_in");

      if (Firebase.pushJSON(firebaseData, "/attendance", json)) {
        Serial.println("Data sent to Firebase successfully");
      } else {
        Serial.println("Firebase data upload failed");
        Serial.println("REASON: " + firebaseData.errorReason());
      }
    }

    // Halt PICC
    rfid.PICC_HaltA();
    // Stop encryption on PCD
    rfid.PCD_StopCrypto1();

    // Keep LED on for 1 second
    delay(1000);

    // Turn off LED
    digitalWrite(LED_PIN, LOW);
  }

  delay(100);  // Short delay in the loop
}

// Connect to WiFi
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Blink LED while connecting
  }
  digitalWrite(LED_PIN, LOW);

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.print("Connected to WiFi with IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println();
    Serial.println("Failed to connect to WiFi.");
    blinkError(2);
  }
}

// Function to print date and time
void printDateTime(const RtcDateTime& dt) {
  char dateString[20];

  snprintf_P(dateString, 
             countof(dateString),
             PSTR("%04u-%02u-%02u %02u:%02u:%02u"),
             dt.Year(),
             dt.Month(),
             dt.Day(),
             dt.Hour(),
             dt.Minute(),
             dt.Second());

  Serial.print("Current time: ");
  Serial.println(dateString);
}

// Function to blink LED for error indication
void blinkError(int blinks) {
  for (int i = 0; i < blinks; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

// Helper function for string buffer sizing
#define countof(a) (sizeof(a) / sizeof(a[0]))

// Function to get current timestamp
String getCurrentTimestamp() {
  RtcDateTime now = rtc.GetDateTime();
  char timestamp[21];
  sprintf(timestamp, "%04u-%02u-%02u %02u:%02u:%02u", 
          now.Year(), now.Month(), now.Day(), 
          now.Hour(), now.Minute(), now.Second());
  return String(timestamp);
}
