# Hack the Planet 🌎
Transform your Flipper Zero into a professional-grade bioelectrical activity monitor! This application automatically detects and adapts to various hardware configurations, providing real-time monitoring of electrical signals from plants, biological samples, and other conductive materials, accompanied by dynamic audio feedback. 
#
#
🔑 **Key Features:**

🔍 Intelligent Hardware Detection – Automatically detects amplifier boards vs direct connections

🎯 Automatic Calibration – Self-calibrating offset compensation for amplified signals

🌱 Dual Monitoring Modes – Direct differential measurement and amplified signal processing

🔊 Dynamic Audio Feedback – Frequency-mapped audio with sensitivity scaling

📊 Real-time Visualization – Live voltage display with error indication

⚡ Robust Error Handling – Comprehensive ADC error detection and recovery

🎛️ Adaptive Sensitivity – Threshold auto-adjustment based on detected hardware

📱 State Machine Architecture – Reliable operation with built-in error recovery
#
#
🧩 **Hardware Configurations:**

Configuration 1: Direct Connection (Basic)

Use for: Learning, experimentation, high-voltage signals
#
GPIO Connections:

Pin 4 (PA4) → Electrode A (Positive)

Pin 6 (PB1) → Electrode B (Negative)
#
Features:

3 Measures differential voltage

High noise immunity (0.1V threshold)

Displays in microvolts (µV)

Configuration 2: Amplifier Board (Advanced)
#
Use for: Sensitive measurements, plant monitoring, research
#
Amplifier Board GPIO Connections:

Pin 2 (PA7) → Amplifier Output

Pin 3 (PA6) → Reference/Detection Pin

Pin 4 (PA4) & Pin 5 (PA5) → Electrodes via amplifier

GND → Amplifier Ground
#
Features:

Millivolt-level sensitivity

Auto offset calibration

10x sensitivity multiplier
#
#
🔧 **Recommended Amplifier Circuit:**

INA128 or AD620-based instrumentation amplifier

Gain: ~100–1000x

High input impedance (>1GΩ)

Low noise, low drift

#
I'm currently in the process of designing the Amplifier Board to accompany this application. Keep an eye out for updates.
#
#
🚀 **Installation:**

Option 1: Precompiled

Download the latest .fap file from Releases

Copy it to your Flipper's apps/GPIO/ folder via qFlipper

Option 2: Build From Source

Clone the Flipper firmware

Place this app into applications_user/

Build:

./fbt fap_hack_the_planet ./fbt launch_app APPID=hack_the_planet
#
#
🎮 **Usage Guide:**

1. Getting Started
#
3. Connect your electrodes (direct or amplifier)
#
4. Launch Hack the Planet from the GPIO apps menu
#
5. The app will automatically:

      Detect configuration

      Calibrate if necessary

      Apply proper sensitivity
 # 
 6. Press OK to begin monitoring
 #
 7. Monitoring States:

       DETECTING – Hardware auto-detection (~2 sec)

       CALIBRATING – Amplifier offset tuning

       READY – Waiting for user

       MONITORING – Active signal analysis with audio

       ERROR – Retry/recover from ADC issues
#
#
🌿 **Electrode Applications:**

Plants

Ideal for pothos, philodendron, rubber plants

Clean electrode surfaces before use

Attach to leaves or stems

Plants may respond to touch, music, light, etc.
#
#
**Other Uses:**

Galvanic Skin Response (GSR)

Electrolyte conductivity

Bioelectricity demos

Fruit/vegetable measurements
#
#
📊 **Technical Specifications:**

Sampling & Signal

Sample Rate: 20Hz (50ms)

ADC: 12-bit (4096 levels)

Voltage: 0–3.3V

Buffer: 128 samples

Frequency Output: 50–2000Hz

Thresholds

Amplified: 10mV, 10x sensitivity

Direct: 100mV, 1x sensitivity

Detection Logic

Amplifier: 1.5–1.8V average w/ <50mV variance

Auto-switch between modes

Error Handling

ADC retries with fallback

NaN/infinity protection

Auto re-init and status messages
#
#
🛠 **Troubleshooting:**

No Hardware Detected

Check the GPIO wiring

Ensure the amplifier ground is connected

Detection pin PA6 must pull low
#
⚠️ **Erratic Readings**

Clean and secure electrodes

Avoid touching during readings

Eliminate nearby EMI
#
🔇 **No Audio**

Confirm the speaker is active

Recheck voltage thresholds

Adjust electrode placement
#
🚫 **ADC Errors**

Restart app

Check all GPIO cables

Retry via the OK button
#
#
🔬 **Scientific & Educational Uses:**

Circadian rhythm & environmental studies

Plant biofeedback experiments

Signal processing education

Electronic circuit & ADC training

Citizen science + agriculture research
#
#
💡 **Advanced Architecture:**

State Machine

DETECTING → CALIBRATING → READY → MONITORING → ERROR

Signal Engine

Drift-compensated baseline

Δ voltage detection

Real-time frequency mapping

Mode-dependent display scaling
#
#
🤝 **Contributing:**

I welcome pull requests and suggestions!
#
Focus areas:

Multi-channel signal support

Amplifier profile library

Mobile integration & export

Data logging tools
#
#
📜 **License:**

MIT License – Open source hardware & software encouraged
#
#
⚠️ **Safety & Disclaimers:**

⚡ Use low voltage only (<5V)

🌱 Do not damage living plants

🧪 Not a medical device

📊 Data is for educational use only
