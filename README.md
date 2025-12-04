# NoSQL Project: IoT Telemetry API

A simple Flask-based API that receives telemetry from devices, caches recent telemetry in Redis, and periodically syncs grouped device data to MongoDB. Includes endpoints to query current Redis cache and persisted groups from MongoDB.

## Prerequisites

- Python 3.10+ (recommended)
- Redis server (local or remote)
- MongoDB (Atlas or self-hosted)
- Git
- Optional: Arduino IDE for `Microcontrollers/esp32-system-monitor.ino`

## Project Structure

- `API/api.py` — Flask API server
- `Microcontrollers/esp32-system-monitor.ino` — Example ESP32 telemetry sender

## Setup

1. Clone the repository:
```
   git clone https://github.com/mkocovic2/NoSQL-Project.git
   cd NoSQL-Project
```

2. Create and activate a virtual environment:
```
   python -m venv .venv
   # Windows
   .venv\Scripts\activate
   # macOS/Linux
   source .venv/bin/activate
```

3. Install dependencies:
```
   pip install flask pymongo flask-cors redis
```

4. Configure MongoDB:
- Create a MongoDB deployment (Atlas or local).
- Get your connection string (URI), e.g. `mongodb+srv://<user>:<pass>@<cluster>/<db>?retryWrites=true&w=majority`.
- Edit `API/api.py` and set the `uri` value:
  ```python
  # MongoDB setup
  uri = "YOUR_MONGODB_URI_HERE"
  ```
- The app uses database `iot_database` and collection `device_groups`. It will attempt to connect and create a unique index on `group_id`.

5. Configure Redis:
- Install and start Redis locally, or use a remote Redis instance.
   - Default connection is `localhost:6379`. To change, edit:
     ```python
     redis_client = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)
     ```

## Running the API

From the repository root (with the virtual environment activated):

```
python API/api.py
```

- The server starts on `http://0.0.0.0:3000` with `debug=True`.
- A background thread runs every hour to sync telemetry from Redis to MongoDB and then clears the Redis telemetry keys.

## Endpoints

- `POST /telemetry`
  - Accepts JSON telemetry.
  - Required fields: `device_id`, `group_id`
  - Optional: `device_type` and any additional metrics.
  - Adds `timestamp` (UTC ISO 8601) and `status="online"`.
  - Stores payload in Redis with key format `telemetry:{group_id}:{device_id}`.

  Example:
```
  {
    "device_id": "device-001",
    "group_id": "group-abc",
    "device_type": "esp32",
    "cpu": 12.3,
    "mem": 45.6
  }
```

- `GET /redis`
- Returns all telemetry payloads currently cached in Redis.
- Response:
  ```json
  {
    "status": "success",
    "count": 2,
    "devices": [ /* telemetry objects */ ]
  }
  ```

- `GET /groups`
- Returns all groups persisted in MongoDB (`device_groups` collection), excluding `_id`.

- `GET /groups/<group_id>`
- Returns details for a specific group, excluding `_id`.

- `GET /health`
- Returns `{ "status": "ok" }` if the server is healthy (and DB is connected).
- `POST /health`
- Alias of `POST /telemetry`.

## Data Flow

- Devices `POST /telemetry` -> stored immediately in Redis.
- Background sync (hourly):
- Aggregates telemetry by `group_id`.
- Upserts documents in MongoDB with:
  - `group_id`
  - `last_updated`
  - `devices` (array of latest telemetry objects)
  - `created_at` (on first insert)
  - Clears synced Redis keys.

## ESP32 Example

Use `Microcontrollers/esp32-system-monitor.ino` as a starting point to send telemetry to:
```
http://<server-ip>:3000/telemetry

```

Ensure your ESP32 has network connectivity and the payload matches the expected fields.

## Troubleshooting

- MongoDB connection errors:
  - Verify `uri` is set and credentials are correct.
  - Make sure IP access list allows your machine (for Atlas).

- Redis connection errors:
  - Ensure Redis is running and reachable.
  - Adjust host/port in `api.py` if using a remote server.

- Background sync not persisting:
  - Check server logs for "Syncing Redis telemetry to MongoDB..." messages.
  - Verify Redis has keys: `telemetry:*`.
  - Confirm MongoDB has documents in `iot_database.device_groups`.
