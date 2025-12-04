#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

// WiFi credentials
const char* ssid = "[REDACTED]";
const char* password = "[REDACTED]";
const char* serverUrl = "[REDACTED]";
const char* DEVICE_ID = "esp32-system-monitor";

// Display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// System monitoring variables
unsigned long bootTime = 0;
unsigned long lastResetReason = 0;
uint32_t loopCounter = 0;
float cpuTemp = 0;

String getGroupId() {
  String gateway = WiFi.gatewayIP().toString();
  gateway.replace(".", "-");
  return "net-" + gateway;
}

void testConnection() {
  HTTPClient http;
  WiFiClientSecure client;
  
  client.setInsecure();
  
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

String getResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch(reason) {
    case ESP_RST_POWERON: return "Power On";
    case ESP_RST_SW: return "Software Reset";
    case ESP_RST_PANIC: return "Panic/Exception";
    case ESP_RST_INT_WDT: return "Interrupt WDT";
    case ESP_RST_TASK_WDT: return "Task WDT";
    case ESP_RST_WDT: return "Other WDT";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO Reset";
    default: return "Unknown";
  }
}

float getCPUTemperature() {
  // Note: ESP32 internal temperature sensor is not very accurate
  // This is an approximation - you may need external sensor for accuracy
  return temperatureRead();
}

String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  String uptime = "";
  if (days > 0) uptime += String(days) + "d ";
  uptime += String(hours % 24) + "h ";
  uptime += String(minutes % 60) + "m ";
  uptime += String(seconds % 60) + "s";
  
  return uptime;
}

void setup() {
  Serial.begin(115200);
  bootTime = millis();
  
  // Get reset reason
  Serial.println("Reset Reason: " + getResetReason());
  
  // Initialize I2C and display
  Wire.begin(21, 22);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Display init failed!");
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("System Monitor");
  display.println("Starting...");
  display.display();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✓ Connected to WiFi!");
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
  loopCounter++;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    
    client.setInsecure();
    
    Serial.println("\n--- System Telemetry POST ---");
    
    http.begin(client, serverUrl);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("ngrok-skip-browser-warning", "true");
    http.addHeader("User-Agent", "ESP32-System-Monitor");
    
    // Gather system telemetry
    unsigned long uptime = millis() - bootTime;
    uint32_t heapFree = ESP.getFreeHeap();
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapUsed = heapTotal - heapFree;
    float heapUsedPercent = (float)heapUsed / heapTotal * 100.0;
    uint32_t psramFree = ESP.getFreePsram();
    uint32_t psramTotal = ESP.getPsramSize();
    uint32_t cpuFreq = ESP.getCpuFreqMHz();
    cpuTemp = getCPUTemperature();
    String chipModel = ESP.getChipModel();
    uint8_t chipCores = ESP.getChipCores();
    uint32_t chipRevision = ESP.getChipRevision();
    uint32_t flashSize = ESP.getFlashChipSize();
    String localIP = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    
    // Create JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"group_id\":\"" + getGroupId(); + "\",";
    jsonPayload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    jsonPayload += "\"device_type\":\"system_monitor\",";
    jsonPayload += "\"uptime_ms\":" + String(uptime) + ",";
    jsonPayload += "\"uptime_str\":\"" + formatUptime(uptime) + "\",";
    jsonPayload += "\"reset_reason\":\"" + getResetReason() + "\",";
    jsonPayload += "\"loop_count\":" + String(loopCounter) + ",";
    jsonPayload += "\"heap_free\":" + String(heapFree) + ",";
    jsonPayload += "\"heap_total\":" + String(heapTotal) + ",";
    jsonPayload += "\"heap_used\":" + String(heapUsed) + ",";
    jsonPayload += "\"heap_used_percent\":" + String(heapUsedPercent, 1) + ",";
    jsonPayload += "\"psram_free\":" + String(psramFree) + ",";
    jsonPayload += "\"psram_total\":" + String(psramTotal) + ",";
    jsonPayload += "\"cpu_freq_mhz\":" + String(cpuFreq) + ",";
    jsonPayload += "\"cpu_temp_c\":" + String(cpuTemp, 1) + ",";
    jsonPayload += "\"chip_model\":\"" + chipModel + "\",";
    jsonPayload += "\"chip_cores\":" + String(chipCores) + ",";
    jsonPayload += "\"chip_revision\":" + String(chipRevision) + ",";
    jsonPayload += "\"flash_size\":" + String(flashSize) + ",";
    jsonPayload += "\"wifi_rssi\":" + String(rssi) + ",";
    jsonPayload += "\"local_ip\":\"" + localIP + "\"";
    jsonPayload += "}";
    
    Serial.println("Payload: " + jsonPayload);
    Serial.println("Payload size: " + String(jsonPayload.length()) + " bytes");
    
    int httpResponseCode = http.POST(jsonPayload);
    
    Serial.println("HTTP Response Code: " + String(httpResponseCode));
    
    // Update display with system info
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    
    // Header
    display.println("System Monitor");
    display.println("---------------");
    
    // Uptime
    display.print("Uptime: ");
    if (uptime < 60000) {
      display.print(uptime / 1000);
      display.println("s");
    } else if (uptime < 3600000) {
      display.print(uptime / 60000);
      display.println("m");
    } else {
      display.print(uptime / 3600000);
      display.println("h");
    }
    
    // Memory usage
    display.print("Mem: ");
    display.print(heapUsedPercent, 0);
    display.print("% (");
    display.print(heapFree / 1024);
    display.println("KB)");
    
    // CPU info
    display.print("CPU: ");
    display.print(cpuFreq);
    display.print("MHz ");
    display.print(cpuTemp, 0);
    display.println("C");
    
    // WiFi signal
    display.print("WiFi: ");
    display.print(rssi);
    display.println(" dBm");
    
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
    Serial.println("WiFi disconnected");
    
    // Show disconnection on display
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("WiFi");
    display.println("Lost!");
    display.display();
  }
  
  delay(5000);  // Send telemetry every 5 seconds
}