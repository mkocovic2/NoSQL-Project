#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// WiFi credentials
const char* ssid = "[REDACTED];
const char* password = "[REDACTED]";
const char* serverUrl = "[REDACTED]";
const char* DEVICE_ID = "esp32-task-monitor";

// Display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Task monitoring variables
#define MAX_TASKS 32
TaskStatus_t taskStatusArray[MAX_TASKS];
uint32_t totalRunTime;
UBaseType_t taskCount;

struct TaskInfo {
  String name;
  uint32_t stackHighWaterMark;
  uint32_t runtime;
  float cpuPercent;
  eTaskState state;
  UBaseType_t priority;
  BaseType_t coreId;
};

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

String getTaskStateName(eTaskState state) {
  switch(state) {
    case eRunning: return "Running";
    case eReady: return "Ready";
    case eBlocked: return "Blocked";
    case eSuspended: return "Suspended";
    case eDeleted: return "Deleted";
    default: return "Unknown";
  }
}

void getTaskInfo(TaskInfo* tasks, UBaseType_t* count) {
  taskCount = uxTaskGetSystemState(taskStatusArray, MAX_TASKS, &totalRunTime);
  *count = taskCount;
  
  // Avoid divide by zero
  if (totalRunTime == 0) totalRunTime = 1;
  
  for (UBaseType_t i = 0; i < taskCount; i++) {
    tasks[i].name = String(taskStatusArray[i].pcTaskName);
    tasks[i].stackHighWaterMark = taskStatusArray[i].usStackHighWaterMark;
    tasks[i].runtime = taskStatusArray[i].ulRunTimeCounter;
    tasks[i].cpuPercent = (float)taskStatusArray[i].ulRunTimeCounter / totalRunTime * 100.0;
    tasks[i].state = taskStatusArray[i].eCurrentState;
    tasks[i].priority = taskStatusArray[i].uxCurrentPriority;
    
    // Get core affinity (ESP32 specific)
    TaskHandle_t handle = taskStatusArray[i].xHandle;
    tasks[i].coreId = xTaskGetAffinity(handle);
  }
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
  display.println("Task Monitor");
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
    
    client.setInsecure();
    
    Serial.println("\n--- Task Telemetry POST ---");
    
    http.begin(client, serverUrl);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("ngrok-skip-browser-warning", "true");
    http.addHeader("User-Agent", "ESP32-Task-Monitor");
    
    // Get task information
    TaskInfo tasks[MAX_TASKS];
    UBaseType_t count;
    getTaskInfo(tasks, &count);
    
    // Calculate totals and find top tasks
    uint32_t totalStackUsed = 0;
    float maxCpuPercent = 0;
    String topCpuTask = "";
    uint32_t minStack = 999999;
    String tightestStackTask = "";
    
    for (UBaseType_t i = 0; i < count; i++) {
      totalStackUsed += tasks[i].stackHighWaterMark;
      
      if (tasks[i].cpuPercent > maxCpuPercent) {
        maxCpuPercent = tasks[i].cpuPercent;
        topCpuTask = tasks[i].name;
      }
      
      if (tasks[i].stackHighWaterMark < minStack) {
        minStack = tasks[i].stackHighWaterMark;
        tightestStackTask = tasks[i].name;
      }
    }
    
    // Create JSON payload
    String jsonPayload = "{";
    jsonPayload += "\"group_id\":\"" + getGroupId(); + "\",";
    jsonPayload += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
    jsonPayload += "\"device_type\":\"task_monitor\",";
    jsonPayload += "\"task_count\":" + String(count) + ",";
    jsonPayload += "\"total_runtime\":" + String(totalRunTime) + ",";
    jsonPayload += "\"total_stack_hwm\":" + String(totalStackUsed) + ",";
    jsonPayload += "\"top_cpu_task\":\"" + topCpuTask + "\",";
    jsonPayload += "\"top_cpu_percent\":" + String(maxCpuPercent, 2) + ",";
    jsonPayload += "\"tightest_stack_task\":\"" + tightestStackTask + "\",";
    jsonPayload += "\"min_stack_hwm\":" + String(minStack) + ",";
    jsonPayload += "\"tasks\":[";
    
    // Add individual task data
    for (UBaseType_t i = 0; i < count; i++) {
      if (i > 0) jsonPayload += ",";
      jsonPayload += "{";
      jsonPayload += "\"name\":\"" + tasks[i].name + "\",";
      jsonPayload += "\"stack_hwm\":" + String(tasks[i].stackHighWaterMark) + ",";
      jsonPayload += "\"runtime\":" + String(tasks[i].runtime) + ",";
      jsonPayload += "\"cpu_percent\":" + String(tasks[i].cpuPercent, 2) + ",";
      jsonPayload += "\"state\":\"" + getTaskStateName(tasks[i].state) + "\",";
      jsonPayload += "\"priority\":" + String(tasks[i].priority) + ",";
      jsonPayload += "\"core\":" + String(tasks[i].coreId);
      jsonPayload += "}";
    }
    
    jsonPayload += "]}";
    
    Serial.println("Payload size: " + String(jsonPayload.length()) + " bytes");
    Serial.println("Task count: " + String(count));
    
    int httpResponseCode = http.POST(jsonPayload);
    
    Serial.println("HTTP Response Code: " + String(httpResponseCode));
    
    // Print task summary to Serial
    Serial.println("\n=== Task Summary ===");
    Serial.printf("Total Tasks: %d\n", count);
    Serial.printf("Top CPU: %s (%.1f%%)\n", topCpuTask.c_str(), maxCpuPercent);
    Serial.printf("Tightest Stack: %s (%d bytes free)\n", tightestStackTask.c_str(), minStack);
    Serial.println("\nTask Details:");
    for (UBaseType_t i = 0; i < count; i++) {
      Serial.printf("  %s: Core%d Pri%d Stack:%d CPU:%.1f%% %s\n",
        tasks[i].name.c_str(),
        tasks[i].coreId,
        tasks[i].priority,
        tasks[i].stackHighWaterMark,
        tasks[i].cpuPercent,
        getTaskStateName(tasks[i].state).c_str()
      );
    }
    
    // Update display with task summary
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    
    // Header
    display.println("Task Monitor");
    display.println("---------------");
    
    // Task count
    display.print("Tasks: ");
    display.println(count);
    
    // Top CPU consumer
    display.print("Top: ");
    display.println(topCpuTask);
    display.print("  CPU: ");
    display.print(maxCpuPercent, 1);
    display.println("%");
    
    // Tightest stack
    display.print("Stack: ");
    display.println(tightestStackTask);
    display.print("  Free: ");
    display.print(minStack);
    display.println("B");
    
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
  
  delay(5000);  // Send telemetry every 5 seconds
}