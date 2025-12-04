#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi credentials
const char* ssid = "[REDACTED]";
const char* password = "[REDACTED]";
const char* serverUrl = "[REDACTED]";
const char* DEVICE_ID = "esp32-wifi-monitor";

// Display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String getGroupId() {
  String gateway = WiFi.gatewayIP().toString();
  gateway.replace(".", "-");
  return "net-" + gateway;
}

void testConnection() {
  HTTPClient http;
  WiFiClientSecure client;
  
  client.setInsecure(); // Bypass SSL certificate verification
  
  Serial.println("\n--- Testing Connection ---");
  Serial.println("URL: " + String(serverUrl));
  
  http.begin(client, serverUrl);
  http.setTimeout(10000);
  http.addHeader("ngrok-skip-browser-warning", "true");
  
  int code = http.GET();
  Serial.println("Test GET response: " + String(code));
  if (code > 0) {
    Serial.println("Response: " + http.getString());
  } else {
    Serial.println("Test failed with error: " + String(code));
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C and display
  Wire.begin(21, 22);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Display init failed!");
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("WiFi Monitor");
  display.println("Starting...");
  display.display();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected to WiFi!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  
  // Show connection success
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
  
  // Test the connection
  testConnection();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    
    client.setInsecure(); // Bypass SSL verification
    
    Serial.println("\n--- Attempting POST ---");
    
    http.begin(client, serverUrl);
    http.setTimeout(15000); // 15 second timeout
    http.addHeader("Content-Type", "application/json");
    http.addHeader("ngrok-skip-browser-warning", "true");
    http.addHeader("User-Agent", "ESP32-Monitor");
    
    // Scan for WiFi networks
    int networkCount = WiFi.scanNetworks();
    
    // Gather telemetry data
    int rssi = WiFi.RSSI();
    String localIP = WiFi.localIP().toString();
    String gateway = WiFi.gatewayIP().toString();
    long channel = WiFi.channel();
    int heapFree = ESP.getFreeHeap();
    
    // Create JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"group_id\":\"" + getGroupId(); + "\",";
    jsonPayload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    jsonPayload += "\"device_type\":\"wifi_monitor\",";
    jsonPayload += "\"rssi\":" + String(rssi) + ",";
    jsonPayload += "\"channel\":" + String(channel) + ",";
    jsonPayload += "\"local_ip\":\"" + localIP + "\",";
    jsonPayload += "\"gateway\":\"" + gateway + "\",";
    jsonPayload += "\"networks_found\":" + String(networkCount) + ",";
    jsonPayload += "\"heap_free\":" + String(heapFree);
    jsonPayload += "}";
    
    Serial.println("Payload: " + jsonPayload);
    Serial.println("Payload size: " + String(jsonPayload.length()) + " bytes");
    
    int httpResponseCode = http.POST(jsonPayload);
    
    Serial.println("HTTP Response Code: " + String(httpResponseCode));
    
    // Update display with telemetry
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    
    // Device ID
    display.println("WiFi Monitor");
    display.println("---------------");
    
    // WiFi signal strength
    display.print("Signal: ");
    display.print(rssi);
    display.println(" dBm");
    
    // Channel
    display.print("Channel: ");
    display.println(channel);
    
    // Networks found
    display.print("Networks: ");
    display.println(networkCount);
    
    // IP Address
    display.print("IP: ");
    display.println(localIP);
    
    // HTTP status
    display.println("---------------");
    if (httpResponseCode > 0) {
      display.print("POST: OK (");
      display.print(httpResponseCode);
      display.println(")");
      String response = http.getString();
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println("Response: " + response);
    } else {
      display.print("POST: FAIL (");
      display.print(httpResponseCode);
      display.println(")");
      Serial.println("Error code: " + String(httpResponseCode));
      
      // Print detailed error info
      if (httpResponseCode == -1) Serial.println("  Connection failed");
      else if (httpResponseCode == -3) Serial.println("  Connection lost");
      else if (httpResponseCode == -11) Serial.println("  Read timeout");
    }
    
    display.display();
    http.end();
    
  } else {
    Serial.println("WiFi disconnected!");
    
    // Show disconnection on display
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("WiFi");
    display.println("Lost");
    display.display();
  }
  
  delay(5000);  // Send every 5 seconds
}