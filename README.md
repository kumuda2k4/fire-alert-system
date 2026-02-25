# 🔥 Fire Alert & Security System (IoT)
### ESP8266 + DHT11 + MQ2 Gas Sensor + Firebase Realtime Database

> A high-reliability early warning node that monitors environmental conditions and triggers multi-stage responses — local buzzer alarm + cloud-based real-time dashboard.

---

## 🌐 Live Dashboard
> *(Add your Vercel deployment URL here after deploying)*

---

## 📸 System Overview

```
┌─────────────┐     ┌──────────────┐     ┌─────────────────────┐
│  DHT11      │────▶│              │────▶│  Firebase Realtime   │
│  MQ2 Gas    │────▶│  ESP8266     │────▶│  Database (Cloud)    │
│  Sensor     │     │  NodeMCU     │     └─────────────────────┘
└─────────────┘     │              │────▶  Local Buzzer Alarm
                    └──────────────┘       (works offline too)
                                                  │
                                                  ▼
                                        ┌─────────────────────┐
                                        │   Web Dashboard      │
                                        │   (HTML + Firebase)  │
                                        └─────────────────────┘
```

---

## 🧰 Hardware Required

| Component | Role |
|---|---|
| ESP8266 NodeMCU | Main controller + Wi-Fi |
| DHT11 Sensor | Temperature & Humidity (fireStatus proxy) |
| MQ2 Gas Sensor | Smoke / LPG / CO detection |
| Active Buzzer | Local pulsing alarm |
| Jumper Wires | Connections |
| Breadboard | Prototyping |

---

## 🔌 Wiring Diagram

```
ESP8266 NodeMCU
│
├── D4  ──────── DHT11 DATA pin
├── A0  ──────── MQ2  AO (Analog Output)
├── D7  ──────── Buzzer (+)
├── 3.3V ─────── DHT11 VCC
├── Vin (5V) ─── MQ2 VCC
└── GND ─────── DHT11 GND | MQ2 GND | Buzzer (-)
```

---

## 📁 Project Structure

```
fire-alert-system/
├── arduino/
│   └── fire_alert_esp8266/
│       └── fire_alert_esp8266.ino   ← Upload this to ESP8266
├── web/
│   └── index.html                   ← Dashboard (deploy to Vercel/GitHub Pages)
└── README.md
```

---

## 🔥 Firebase Setup (Step-by-Step)

### Step 1 — Create a Firebase Project
1. Go to [https://console.firebase.google.com](https://console.firebase.google.com)
2. Click **Add project** → name it `fire-alert-system` → Continue
3. Disable Google Analytics (not needed) → **Create project**

### Step 2 — Enable Realtime Database
1. Left sidebar → **Build** → **Realtime Database**
2. Click **Create Database** → Choose a region → **Start in Test mode**
3. Click **Enable**

### Step 3 — Set Database Rules
Go to **Realtime Database → Rules** tab → paste and **Publish**:
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

### Step 4 — Get `DATABASE_URL` and `DATABASE_SECRET` (for Arduino)

| Key | Where to find it |
|---|---|
| `DATABASE_URL` | Realtime Database page — the URL shown at the top (e.g. `https://fire-alert-system-xxxxx-default-rtdb.firebaseio.com`) |
| `DATABASE_SECRET` | ⚙️ Project Settings → **Service Accounts** tab → scroll to **Database Secrets** → click **Show** |

Paste these into the `.ino` file:
```cpp
#define DATABASE_URL    "https://your-project-default-rtdb.firebaseio.com"
#define DATABASE_SECRET "your-database-secret-here"
```

### Step 5 — Get Web App Config (for `index.html`)
1. ⚙️ Project Settings → **General** tab
2. Scroll to **Your apps** → click `</>` (Web) → register app
3. Copy the `firebaseConfig` object and paste into `index.html`:
```javascript
const firebaseConfig = {
  apiKey:            "YOUR_API_KEY",
  authDomain:        "YOUR_PROJECT_ID.firebaseapp.com",
  databaseURL:       "https://YOUR_PROJECT_ID-default-rtdb.firebaseio.com",
  projectId:         "YOUR_PROJECT_ID",
  storageBucket:     "YOUR_PROJECT_ID.appspot.com",
  messagingSenderId: "YOUR_SENDER_ID",
  appId:             "YOUR_APP_ID"
};
```

---

## ⚙️ Arduino IDE Setup

### Install the ESP8266 Board
1. **File → Preferences** → paste in Additional Board URLs:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
2. **Tools → Board → Boards Manager** → search `esp8266` → Install
3. **Tools → Board → NodeMCU 1.0 (ESP-12E Module)**
4. **Tools → Upload Speed → 115200**

### Install Required Libraries
**Sketch → Include Library → Manage Libraries** → search and install:

| Library | Author |
|---|---|
| **DHT sensor library** | Adafruit |
| **Adafruit Unified Sensor** | Adafruit |

> `ESP8266HTTPClient`, `WiFiClientSecure`, and `time.h` are **built-in** — no separate install needed.

### Configure the Sketch
Open `arduino/fire_alert_esp8266/fire_alert_esp8266.ino` and fill in:
```cpp
#define WIFI_SSID       "Your_WiFi_Name"
#define WIFI_PASSWORD   "Your_WiFi_Password"
#define DATABASE_URL    "https://your-project-default-rtdb.firebaseio.com"
#define DATABASE_SECRET "your-database-secret"
```

### Upload
1. Connect ESP8266 via USB
2. **Tools → Port** → select your COM/USB port
3. Click **Upload** (→)
4. Open **Serial Monitor** at `115200 baud` to watch live output

---

## 📊 Firebase Database Structure

Once the ESP8266 connects and runs, it creates this structure automatically:

```
/
├── sensor/
│   ├── fireStatus    : false          ← true when temp ≥ 33°C
│   ├── gasLevel      : 154            ← MQ2 analog reading (0–1023)
│   ├── temperature   : 28.4           ← DHT11 reading in °C
│   ├── humidity      : 62.0           ← DHT11 humidity %
│   ├── alertActive   : false          ← Is alert currently triggered?
│   ├── alertTime     : "2024-01-15 14:23:01"
│   └── lastUpdated   : "2024-01-15 14:23:05"
│
└── alerts/
    └── 2024-01-15_14-23-01/           ← One node per alert event
        ├── fireStatus  : true
        ├── gasLevel    : 712
        ├── temperature : 38.2
        ├── alertTime   : "2024-01-15 14:23:01"
        └── reason      : "TEMP_HIGH"
```

---

## 🧠 System Intelligence

### Threshold Logic
| Condition | Trigger |
|---|---|
| `temperature >= 33°C` | `fireStatus = true` |
| `gasLevel >= baseline + 80` | Gas alert triggered |
| Either condition true | Buzzer starts pulsing, Firebase logs event |

### Auto-Reset (5-Second Hold)
Once both sensors return to safe values, the system waits **5 continuous seconds** before resetting to `SAFE` state — prevents false resets from brief dips.

### Offline / Autonomous Mode
The buzzer triggers **entirely on local logic** — no Wi-Fi needed. The ESP8266 keeps alarming even if Firebase is unreachable.

### Calibration at Boot
On every boot, the MQ2 takes **20 readings in clean air** over ~2 seconds and sets a personal baseline. The alert threshold is `baseline + 80` — avoiding false positives from room-to-room variation.

---

## 🌍 Deploy Dashboard to Vercel

### Step 1 — Push to GitHub
```bash
git clone https://github.com/kumuda2k4/fire-alert-system.git
cd fire-alert-system

# Make your changes, then:
git add .
git commit -m "Add dashboard and Arduino code"
git push origin main
```

### Step 2 — Deploy on Vercel
1. Go to [https://vercel.com](https://vercel.com) → Sign up with GitHub
2. Click **Add New → Project**
3. Import `fire-alert-system` repository
4. **Framework Preset:** Other (it's a plain HTML file)
5. **Root Directory:** `web/` (where `index.html` lives)
6. Click **Deploy**
7. Your dashboard is live at `https://fire-alert-system-xxx.vercel.app`

### Step 3 — Auto-Deploy on Every Push
Every time you run `git push`, Vercel automatically rebuilds and redeploys. No manual steps needed.

---

## 🧪 Test Without Hardware

To verify the dashboard works before your hardware is ready:
1. Open Firebase Console → Realtime Database
2. Manually add these values under `/sensor`:
   ```
   fireStatus   → true
   gasLevel     → 750
   temperature  → 36.5
   alertActive  → true
   ```
3. The dashboard should **immediately switch to red FIRE ALERT** status

---

## ❓ Troubleshooting

| Problem | Fix |
|---|---|
| Firebase push returns 401 | Regenerate Database Secret in Project Settings |
| `DATABASE_URL` error | Make sure URL includes `https://` in the `.ino`, no trailing `/` |
| DHT11 always reads NaN | Wait 2+ seconds after power-on; check D4 wiring |
| MQ2 always reads 0 | Check AO → A0 wiring; give 15–30s warmup |
| Serial Monitor shows garbage | Set baud rate to `115200` exactly |
| Vercel shows blank page | Set Root Directory to `web/` in Vercel project settings |
| Dashboard not updating | Check Firebase Rules are set to public read/write |

---

## 🔒 Security Note
Current Firebase rules allow **public read and write** — suitable for development and college projects. For a production deployment, add Firebase Authentication and restrict write access to authenticated devices only.

---

## 👤 Author
**Kumuda** — [github.com/kumuda2k4](https://github.com/kumuda2k4)

---

*Built as part of an IoT Engineering project — ESP8266 + Firebase + Real-Time Web Dashboard*
