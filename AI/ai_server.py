import cv2
import numpy as np
import paho.mqtt.client as mqtt
from flask import Flask, request
from ultralytics import YOLO
import threading
import requests  # <--- [เพิ่ม] สำหรับส่งเข้า Discord

# ==========================================
# 1. ตั้งค่า NETPIE
# ==========================================
NETPIE_HOST = "broker.netpie.io"
CLIENT_ID = "45bba792-13b8-47ce-83e7-fb23cc57231f"
TOKEN = "c4wx1b74Z3Nn9eHvzTdHQ7BhttUNP4mi"
SECRET = "qmvRJrcN7aTLX9uMvDdxsJ4h4BFe1DsX"
TOPIC_STATUS = "@msg/status" 

# ==========================================
# [เพิ่ม] ตั้งค่า Discord Webhook
# ==========================================
# เอา Link ที่ Copy จาก Discord มาใส่ในเครื่องหมายคำพูดนี้
DISCORD_WEBHOOK_URL = "https://discordapp.com/api/webhooks/1440695358698029056/TBaoO1TacVQooZ0x0T3aKG_XkIL16ixgmMO5u-KIcfmCJV9Woxzvmev_BvCOHKJDOV-g" 

mqtt_client = None

def on_connect(client, userdata, flags, rc):
    print(f"✅ Connected to NETPIE with result code {rc}")

def start_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client(protocol=mqtt.MQTTv311, client_id=CLIENT_ID)
    mqtt_client.username_pw_set(TOKEN, SECRET)
    mqtt_client.on_connect = on_connect
    try:
        mqtt_client.connect(NETPIE_HOST, 1883, 60)
        mqtt_client.loop_forever()
    except Exception as e:
        print(f"❌ Cannot connect to NETPIE: {e}")

mqtt_thread = threading.Thread(target=start_mqtt)
mqtt_thread.daemon = True
mqtt_thread.start()

# ==========================================
# [เพิ่ม] ฟังก์ชันส่งแจ้งเตือนเข้า Discord
# ==========================================
def send_discord_alert(message):
    try:
        data = {
            "content": message,
            "username": "Hamster Alert Bot" # ชื่อที่จะขึ้นใน Discord
        }
        response = requests.post(DISCORD_WEBHOOK_URL, json=data)
        if response.status_code == 204:
            print("🔔 Discord Alert Sent!")
        else:
            print(f"⚠️ Discord Send Failed: {response.status_code}")
    except Exception as e:
        print(f"❌ Error sending to Discord: {e}")

# ==========================================
# 2. โหลดโมเดล AI
# ==========================================
print("⏳ Loading AI Model...")
model = YOLO('cup_model.pt') 
print("✅ Model Loaded!")

# ==========================================
# 3. สร้าง Web Server
# ==========================================
app = Flask(__name__)

@app.route('/upload', methods=['POST'])
def upload_file():
    if 'imageFile' not in request.files:
        return "No image sent", 400
        
    file = request.files['imageFile']
    npimg = np.frombuffer(file.read(), np.uint8)
    img = cv2.imdecode(npimg, cv2.IMREAD_COLOR)
    print("\n--- Received an image! Processing... ---")

    # --- ให้ AI ทำนายผล ---
    results = model.predict(img, conf=0.5, verbose=False)
    
    status = "not_found"
    found_any_cup = False
    is_tipped = False

    for result in results:
        if len(result.boxes) > 0:
            found_any_cup = True
            for box in result.boxes:
                class_id = int(box.cls[0])
                class_name = model.names[class_id]
                confidence = box.conf[0]
                print(f"AI detected: {class_name} ({confidence:.2f})")
                
                if class_name == 'tipped':
                    is_tipped = True
    
    # สรุปสถานะสุดท้าย
    if is_tipped:
        status = "tipped"
    elif found_any_cup:
        status = "normal"
    else:
        status = "not_found"

    # --- [ส่วนใหม่] เช็คเงื่อนไขแจ้งเตือน Discord ---
    if status == "tipped":
        # ถ้าแก้วหก ให้ส่งแจ้งเตือน!
        msg = f"🚨 **แจ้งเตือนด่วน!** AI ตรวจพบ **ชามข้าวหก (Tipped)** ⚠️"
        send_discord_alert(msg)
    
    # (ถ้าอยากให้แจ้งเตือนตอนหาไม่เจอด้วย ก็เปิดบรรทัดล่างนี้)
    # elif status == "not_found":
    #     send_discord_alert("⚠️ แจ้งเตือน: AI มองไม่เห็นแก้วน้ำ (Not Found)")

    # --- ส่งผลลัพธ์ไป NETPIE ---
    if mqtt_client and mqtt_client.is_connected():
        mqtt_client.publish(TOPIC_STATUS, status)
        print(f"📡 Published to NETPIE: {status}")
    else:
        print("⚠️ NETPIE not connected. Cannot publish.")

    return f"Processed: {status}", 200

if __name__ == '__main__':
    print("🚀 Server is starting on port 5001...")
    app.run(host='0.0.0.0', port=5001)