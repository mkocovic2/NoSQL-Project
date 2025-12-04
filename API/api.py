from flask import Flask, request, jsonify
from pymongo.mongo_client import MongoClient
from pymongo.server_api import ServerApi
from datetime import datetime
from flask_cors import CORS
import redis
import threading
import time
import json

app = Flask(__name__)
CORS(app)

# MongoDB setup
uri = ""
client = None
db = None
collection = None

try:
    client = MongoClient(uri, server_api=ServerApi('1'))
    client.admin.command('ping')
    print("Pinged your deployment. You successfully connected to MongoDB!")
    db = client['iot_database']
    collection = db['device_groups']
    collection.create_index('group_id', unique=True)
except Exception as e:
    print(f"MongoDB connection failed: {e}")
    print("Server will start but database operations will fail!")

# Redis setup, use optional server if needed. Default is localhost.
redis_client = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)

def redis_key(group_id, device_id):
    return f"telemetry:{group_id}:{device_id}"

@app.route('/telemetry', methods=['POST'])
def receive_telemetry():
    try:
        data = request.get_json()
        timestamp = datetime.utcnow().isoformat()
        device_id = data.get('device_id')
        group_id = data.get('group_id')
        device_type = data.get('device_type')

        if not device_id or not group_id:
            return jsonify({"status": "error", "message": "device_id and group_id required"}), 400

        # Add timestamp and status
        data['timestamp'] = timestamp
        data['status'] = "online"
        data['device_type'] = device_type

        # Store in Redis
        key = redis_key(group_id, device_id)
        redis_client.set(key, json.dumps(data))

        print(f"Telemetry stored in Redis: {key}")

        return jsonify({
            "status": "success",
            "group_id": group_id,
            "device_id": device_id
        }), 200

    except Exception as e:
        print(f"Error: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/redis', methods=['GET'])
def get_redis_data():
    """Get all telemetry data from Redis"""
    try:
        keys = redis_client.keys("telemetry:*")
        devices = []
        for key in keys:
            data = redis_client.get(key)
            if data:
                devices.append(json.loads(data))
        return jsonify({
            "status": "success",
            "count": len(devices),
            "devices": devices
        }), 200
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

def sync_redis_to_mongo():
    while True:
        try:
            print("Syncing Redis telemetry to MongoDB...")
            keys = redis_client.keys("telemetry:*")
            group_devices = {}
            for key in keys:
                data = redis_client.get(key)
                if data:
                    telemetry = json.loads(data)
                    group_id = telemetry.get('group_id')
                    device_id = telemetry.get('device_id')
                    if group_id and device_id:
                        if group_id not in group_devices:
                            group_devices[group_id] = []
                        group_devices[group_id].append(telemetry)
            # Update MongoDB
            for group_id, devices in group_devices.items():
                timestamp = datetime.utcnow()
                if collection is not None:
                    collection.update_one(
                        {'group_id': group_id},
                        {
                            '$set': {
                                'group_id': group_id,
                                'last_updated': timestamp,
                                'devices': devices
                            },
                            '$setOnInsert': {
                                'created_at': timestamp
                            }
                        },
                        upsert=True
                    )
                    print(f"Synced group {group_id} with {len(devices)} devices to MongoDB")
            # Clear Redis
            for key in keys:
                redis_client.delete(key)
            print("Redis telemetry cleared after sync")
        except Exception as e:
            print(f"Redis-to-Mongo sync error: {e}")
        time.sleep(3600)  # Wait 1 hour

# Start background sync thread
sync_thread = threading.Thread(target=sync_redis_to_mongo, daemon=True)
sync_thread.start()

@app.route('/groups', methods=['GET'])
def get_groups():
    if collection is None:
        return jsonify({"status": "error", "message": "Database not connected"}), 503
    try:
        groups = list(collection.find({}, {'_id': 0}))
        return jsonify({
            "status": "success",
            "count": len(groups),
            "groups": groups
        }), 200
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/groups/<group_id>', methods=['GET'])
def get_group(group_id):
    if collection is None:
        return jsonify({"status": "error", "message": "Database not connected"}), 503
    try:
        group = collection.find_one({'group_id': group_id}, {'_id': 0})
        if group:
            return jsonify({
                "status": "success",
                "group": group
            }), 200
        else:
            return jsonify({
                "status": "error",
                "message": "Group not found"
            }), 404
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/health', methods=['GET', 'POST'])
def health():
    if collection is None:
        return jsonify({"status": "error", "message": "Database not connected"}), 503
    if request.method == 'POST':
        return receive_telemetry()
    return jsonify({"status": "ok"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=3000, debug=True)
