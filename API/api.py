from flask import Flask, request, jsonify
from pymongo.mongo_client import MongoClient
from pymongo.server_api import ServerApi
from datetime import datetime
from flask_cors import CORS

app = Flask(__name__)
CORS(app) 

uri = ""

client = None
db = None
collection = None

try:
    client = MongoClient(uri, server_api=ServerApi('1'))
    client.admin.command('ping')
    print("✓ Pinged your deployment. You successfully connected to MongoDB!")
    
    db = client['iot_database']
    collection = db['device_groups']
    
    collection.create_index('group_id', unique=True)
    
except Exception as e:
    print(f"MongoDB connection failed: {e}")
    print("Server will start but database operations will fail!")

@app.route('/telemetry', methods=['POST'])
def receive_telemetry():
    if collection is None:
        return jsonify({"status": "error", "message": "Database not connected"}), 503
    
    try:
        data = request.get_json()
        timestamp = datetime.utcnow()
        
        # Extract required fields
        device_id = data.get('device_id')
        group_id = data.get('group_id')
        device_type = data.get('device_type')
        
        if not device_id or not group_id:
            return jsonify({"status": "error", "message": "device_id and group_id required"}), 400
        
        # Prepare device data
        device_data = {
            "device_id": device_id,
            "device_type": device_type,
            "timestamp": timestamp,
            "status": "online"
        }
        
        # Add all telemetry data to device
        for key, value in data.items():
            if key not in ['device_id', 'group_id', 'device_type']:
                device_data[key] = value
        
        # Update or insert group document
        result = collection.update_one(
            {'group_id': group_id},
            {
                '$set': {
                    'group_id': group_id,
                    'last_updated': timestamp
                },
                '$setOnInsert': {
                    'created_at': timestamp
                }
            },
            upsert=True
        )
        
        # Update or add device within the group
        collection.update_one(
            {'group_id': group_id},
            {
                '$pull': {'devices': {'device_id': device_id}}  # Remove old entry
            }
        )
        
        collection.update_one(
            {'group_id': group_id},
            {
                '$push': {'devices': device_data}  # Add new entry
            }
        )
        
        print(f"✓ Device updated: {group_id}/{device_id}")
        
        return jsonify({
            "status": "success",
            "group_id": group_id,
            "device_id": device_id
        }), 200
        
    except Exception as e:
        print(f"✗ Error: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/groups', methods=['GET'])
def get_groups():
    """Get all groups"""
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
    """Get specific group by ID"""
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