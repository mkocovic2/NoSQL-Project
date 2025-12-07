# NoSQL Project: IoT Telemetry System
### Michael Kocovic, Alex Cheung

A simple Flask-based API that receives telemetry from devices, caches recent telemetry in Redis, and periodically syncs grouped device data to MongoDB. Includes endpoints to query current Redis cache and persisted groups from MongoDB.

## Prerequisites

- Python 3.10+ (recommended)
- Redis server (local or remote)
- MongoDB (Atlas or self-hosted)
- Git
- Optional: Arduino IDE for ESP32 microcontrollers

## Setup

### 1. Clone the Repository
```bash
git clone https://github.com/mkocovic2/NoSQL-Project.git
cd NoSQL-Project
```

### 2. Create Virtual Environment
```bash
python -m venv .venv

# Windows
.venv\Scripts\activate

# macOS/Linux
source .venv/bin/activate
```

### 3. Install Dependencies
```bash
pip install flask pymongo flask-cors redis
```

### 4. Configure MongoDB

1. Create a MongoDB deployment (Atlas or local)
2. Get your connection string (URI):
   ```
   mongodb+srv://<user>:<pass>@<cluster>/<db>?retryWrites=true&w=majority
   ```
3. Edit `API/api.py` and set the `uri` value:
   ```python
   # MongoDB setup
   uri = "YOUR_MONGODB_URI_HERE"
   ```
4. The API uses database `iot_database` with collections named `group_{group_id}`
5. Indexes are automatically created on: `device_id`, `status`, `device_type`, `last_updated`, `timestamp`

### 5. Configure Redis

Install and start Redis locally, or use a remote Redis instance (We used Redis Cloud).

**For local Redis (default):**
```python
redis_client = redis.Redis(
    host='localhost',
    port=6379,
    decode_responses=True
)
```

**For remote Redis:**
```python
redis_client = redis.Redis(
    host='your-redis-host',
    port=13257,
    decode_responses=True,
    username="your-username",
    password="your-password"
)
```

## Running the API

From the repository root (with virtual environment activated):

```bash
python API/api.py
```

- Server starts on `http://0.0.0.0:3000` with debug mode enabled
- Background thread runs every **1 hour** to sync Redis telemetry to MongoDB
- Console will display sync status: "Syncing Redis telemetry to MongoDB..."

## API Endpoints

### Device Telemetry

**`POST /telemetry`** - Store telemetry in Redis (recommended for high-frequency writes)
- **Required fields:** `device_id`, `group_id`
- **Optional fields:** `device_type`, plus any device-specific metrics
- Automatically adds `timestamp` (UTC ISO 8601) and `status="online"`
- Stores in Redis with key format: `telemetry:{group_id}:{device_id}`

Example request:
```json
{
  "device_id": "esp32-wifi-monitor",
  "group_id": "net_192_168_1_1",
  "device_type": "wifi_monitor",
  "wifi_ssid": "HomeNetwork",
  "wifi_rssi": -45,
  "local_ip": "192.168.1.100"
}
```

**`POST /telemetry/mongo`** - Store telemetry directly in MongoDB (bypasses Redis)
- Same fields as `/telemetry`
- Use for critical data requiring immediate persistence

### Query Endpoints

**`GET /redis`** - View current Redis cache
```json
{
  "status": "success",
  "count": 3,
  "devices": [/* array of telemetry objects */]
}
```

**`GET /groups`** - List all device groups
```json
{
  "status": "success",
  "count": 1,
  "groups": [
    {
      "group_id": "net_192_168_1_1",
      "device_count": 3
    }
  ]
}
```

**`GET /groups/<group_id>`** - Get all devices in a group
```json
{
  "status": "success",
  "group_id": "net_192_168_1_1",
  "device_count": 3,
  "devices": [
    {
      "device_id": "esp32-wifi-monitor",
      "device_type": "wifi_monitor",
      "status": "online",
      "timestamp": "2025-01-15T14:30:00.000Z",
      "last_updated": "2025-01-15T15:00:00.000Z",
      "telemetry": {/* full telemetry data */}
    }
  ]
}
```

**`GET /health`** - Health check
**`POST /health`** - Alias for `POST /telemetry`

## System Architecture

### MongoDB Document Structure

Each device maintains its own document in a group collection:

```json
{
  "device_id": "esp32-system-monitor",
  "group_id": "net_192_168_1_1",
  "device_type": "system_monitor",
  "status": "online",
  "timestamp": "2025-01-15T14:30:00.000Z",
  "last_updated": "2025-01-15T15:00:00.000Z",
  "created_at": "2025-01-10T08:00:00.000Z",
  "telemetry": {
    "device_id": "esp32-system-monitor",
    "cpu_temp_c": 45.2,
    "cpu_freq_mhz": 240,
    "free_heap": 234567,
    "wifi_rssi": -52
  }
}
```

### Device Types

1. **WiFi Monitor** - Network connectivity tracking
   - Fields: `wifi_ssid`, `wifi_rssi`, `local_ip`, `connection_quality`

2. **System Monitor** - Hardware health metrics
   - Fields: `cpu_temp_c`, `cpu_freq_mhz`, `free_heap`, `wifi_rssi`

3. **Task Monitor** - Software performance tracking
   - Fields: `task_count`, `cpu_usage_percent`, `task_delays`

## ESP32 Configuration

### Prerequisites
- Arduino IDE with ESP32 board support
- WiFi credentials
- Server IP address running the Flask API

### Example Code Structure
```cpp
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverUrl = "http://<server-ip>:3000/telemetry";

void setup() {
  // Connect to WiFi
  // Initialize device-specific sensors
}

void loop() {
  // Collect telemetry
  // POST to /telemetry endpoint
  delay(5000); // Send every 5 seconds
}
```

### Upload Instructions
1. Open `.ino` file in Arduino IDE
2. Select **ESP32 Dev Module** as board
3. Configure WiFi credentials and server URL
4. Upload to ESP32
5. Monitor Serial output (115200 baud) for connection status

## Performance Characteristics

- **Write throughput:** ~2,000 writes/second to Redis (buffered)
- **Data generation:** 17,280 records/day per device
- **Sync frequency:** Every 3,600 seconds (1 hour)
- **Scalability:** Designed for horizontal scaling via MongoDB sharding on `group_id`

## MongoDB Query Examples

```javascript
// Get current device status
db.group_net_192_168_1_1.findOne(
  { "device_id": "esp32-system-monitor" },
  { "_id": 0, "telemetry": 1 }
)

// List all devices in location
db.group_net_192_168_1_1.find(
  {},
  { "_id": 0, "device_id": 1, "device_type": 1, "status": 1 }
).sort({ "device_type": 1 })

// Count devices by type
db.group_net_192_168_1_1.aggregate([
  { $group: { _id: "$device_type", count: { $sum: 1 } } }
])
```
