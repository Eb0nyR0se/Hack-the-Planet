# Hack the Planet - Flipper Zero Bioelectrical Monitor 
#

Transform your Flipper Zero into a professional-grade bioelectrical activity monitor! This application automatically detects and adapts to various hardware configurations, providing real-time monitoring of electrical signals from plants, biological samples, and other conductive materials, accompanied by dynamic audio feedback.

*"Hack the Planet is a fun Flipper Zero application that lets you record and explore plant signals and bat sonar. It's perfect for curious nature lovers who want to connect technology with the natural world and unlock hidden signals all around us."*

**Repository:** https://github.com/Eb0nyR0se/HackThePlanet

## Key Features:

- **Intelligent Hardware Detection** – Automatically detects amplifier boards vs direct connections
- **Automatic Calibration** – Self-calibrating offset compensation for amplified signals
- **Dual Monitoring Modes** – Direct differential measurement and amplified signal processing
- **Dynamic Audio Feedback** – Frequency-mapped audio with sensitivity scaling
- **Real-time Visualization** – Live voltage display with error indication
- **Robust Error Handling** – Comprehensive ADC error detection and recovery
- **Adaptive Sensitivity** – Threshold auto-adjustment based on detected hardware
- **State Machine Architecture** – Reliable operation with built-in error recovery

## Installation

### Prerequisites

- Flipper Zero device with updated firmware
- qFlipper application installed on your computer
- MicroSD card inserted in your Flipper Zero

### Option 1: Install Precompiled Binary (Recommended)

1. **Download the Application**
   - Go to the [Releases](https://github.com/Eb0nyR0se/HackThePlanet/releases) page
   - Download the latest `hack_the_planet.fap` file
   - *Note: If no releases are available yet, use Option 2 to build from source*

2. **Connect Your Flipper Zero**
   - Connect your Flipper Zero to your computer via USB
   - Launch qFlipper application
   - Wait for your device to be recognized

3. **Install the App**
   - In qFlipper, navigate to the file browser
   - Browse to the `apps/GPIO/` folder on your Flipper's SD card
   - If the `GPIO` folder doesn't exist, create it
   - Copy the `hack_the_planet.fap` file into the `apps/GPIO/` folder

4. **Launch the App**
   - Disconnect your Flipper Zero from the computer
   - On your Flipper, navigate to: **Apps → GPIO → Hack the Planet**
   - Press OK to launch

### Option 2: Build from Source

1. **Download This Repository**
   - Clone or download this repository:
     ```bash
     git clone https://github.com/yourusername/hack_the_planet.git
     ```

2. **Set Up Flipper Firmware**
   - Clone the official Flipper Zero firmware repository:
     ```bash
     git clone --recursive https://github.com/flipperdevices/flipperzero-firmware.git
     cd flipperzero-firmware
     ```

3. **Add This Application**
   - Copy the `hack_the_planet` folder from this repository into the firmware's `applications_user/` directory:
     ```bash
     cp -r /path/to/downloaded/hack_the_planet ./applications_user/
     ```
   - The structure should look like:
     ```
     flipperzero-firmware/
     └── applications_user/
         └── hack_the_planet/
             ├── application.fam
             ├── hack_the_planet.c
             └── [other source files]
     ```

4. **Build the Application**
   ```bash
   ./fbt fap_hack_the_planet
   ```

5. **Install to Device**
   - **Option A: Direct Install**
     ```bash
     ./fbt launch_app APPID=hack_the_planet
     ```
   
   - **Option B: Manual Install**
     - The compiled `.fap` file will be in `dist/f7-C/`
     - Copy it to your Flipper's `apps/GPIO/` folder using qFlipper

### Option 3: Using the Provided Build Script

If you downloaded the repository and it includes the build script:

1. **Make the script executable:**
   ```bash
   chmod +x build_and_launch.sh
   ```

2. **Run the script:**
   ```bash
   ./build_and_launch.sh
   ```

This will automatically build and launch the application on your connected Flipper Zero.

## Hardware Setup

### Configuration 1: Direct Connection (Basic)

Use for learning, experimentation, and high-voltage signals.

**GPIO Connections:**
- Pin 2 (PA7) → Electrode A (Positive / Signal Input)
- Pin 7 (GND) → Electrode B (Negative / Ground Reference)

**Features:**
- Measures differential voltage
- High noise immunity (0.1V threshold)
- Displays in microvolts (µV)

### Configuration 2: Amplifier Board (Advanced)

Use for sensitive measurements, plant monitoring, and research.

**Amplifier Board GPIO Connections:**
- Pin 2 (PA7) → Amplifier Output
- Pin 3 (PA6) → Reference/Detection Pin
- Pin 4 (PA4) & Pin 5 (PA5) → Electrodes via amplifier
- GND → Amplifier Ground (common ground)

**Features:**
- Millivolt-level sensitivity
- Auto offset calibration
- 10x sensitivity multiplier

## Quick Start Guide

**Never used a Flipper Zero app before?**

1. Install the app using Option 1 (precompiled) or Option 2 (build from source)
2. Connect two jumper wires: Pin 2 (PA7) to one electrode, Pin 7 (GND) to the other
3. On your Flipper: Apps → GPIO → Hack the Planet
4. Press OK to start, then OK again when it shows "READY"
5. Touch the electrodes to a plant leaf and watch the voltage readings!

## Firmware Compatibility

- **Tested on:** Official Flipper Zero firmware v0.100.0 and later
- **Compatible with:** Unleashed, RogueMaster, and other custom firmware forks
- **Minimum required:** Flipper Zero firmware with fap support (v0.74.0+)

1. **Connect Your Hardware**
   - Set up electrodes according to your chosen configuration
   - Ensure all connections are secure

2. **Launch the Application**
   - Navigate to Apps → GPIO → Hack the Planet
   - Press OK to start

3. **Automatic Setup**
   - The app will automatically detect your configuration
   - Calibration will occur if using an amplifier board
   - Proper sensitivity will be applied

4. **Begin Monitoring**
   - Press OK when the app shows "READY"
   - The app will enter monitoring mode with audio feedback

## Monitoring States

- **DETECTING** – Hardware auto-detection (~2 seconds)
- **CALIBRATING** – Amplifier offset tuning
- **READY** – Waiting for user input
- **MONITORING** – Active signal analysis with audio
- **ERROR** – Retry/recover from ADC issues

## Troubleshooting

### No Hardware Detected
- Check GPIO wiring connections
- Ensure amplifier ground is connected
- Verify detection pin PA6 pulls low

### Erratic Readings
- Clean and secure electrodes
- Avoid touching during readings
- Eliminate nearby electromagnetic interference

### No Audio
- Confirm speaker is active
- Check voltage thresholds
- Adjust electrode placement

### ADC Errors
- Restart the application
- Check all GPIO cable connections
- Use OK button to retry

## Technical Specifications

**Sampling & Signal:**
- Sample Rate: 20Hz (50ms)
- ADC: 12-bit (4096 levels)
- Voltage Range: 0–3.3V
- Buffer: 128 samples
- Frequency Output: 50–2000Hz

**Thresholds:**
- Amplified: 10mV, 10x sensitivity
- Direct: 100mV, 1x sensitivity

## Safety & Disclaimers

- Use low voltage only (<5V)
- Do not damage living plants
- Not a medical device
- Data is for educational use only

## License

MIT License – Open source hardware & software encouraged

## Contributing

Pull requests and suggestions are welcome! Focus areas include:
- Multi-channel signal support
- Amplifier profile library
- Mobile integration & export
- Data logging tools
