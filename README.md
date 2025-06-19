# Hack the Planet - Flipper Zero Advanced Bioelectrical Monitor

Transform your Flipper Zero into a professional-grade bioelectrical activity monitor! This application automatically detects and adapts to various hardware configurations, providing real-time monitoring of electrical signals from plants, biological samples, and other conductive materials, accompanied by audio feedback.

## Key Features

- 🔍 **Intelligent Hardware Detection** - Automatically detects amplifier boards vs direct connections
- 🎯 **Automatic Calibration** - Self-calibrating offset compensation for amplified signals
- 🌱 **Dual Monitoring Modes** - Direct differential measurement and amplified signal processing
- 🔊 **Dynamic Audio Feedback** - Frequency-mapped audio with sensitivity scaling
- 📊 **Real-time Visualization** - Live voltage display with error indication
- ⚡ **Robust Error Handling** - Comprehensive ADC error detection and recovery
- 🎛️ **Adaptive Sensitivity** - Automatic threshold adjustment based on detected hardware
- 📱 **State Machine Architecture** - Reliable operation with proper error recovery

## Hardware Configurations

### Configuration 1: Direct Connection (Basic)
**Best for:** Learning, experimentation, high-voltage signals
- 2x Electrodes connected directly to GPIO pins 4 & 5
- Measures differential voltage between electrodes
- Higher voltage threshold (0.1V) for noise immunity
- Microvolt-level sensitivity display

**GPIO Connections:**
```
Pin 4 (PA4) -> Electrode A (Positive)
Pin 5 (PA5) -> Electrode B (Negative)
```

### Configuration 2: Amplifier Board (Advanced)
**Best for:** Sensitive measurements, plant monitoring, research
- External op-amp board for signal conditioning
- Automatic detection via reference pin pulldown
- Millivolt-level sensitivity with offset compensation
- Lower voltage threshold (0.01V) for sensitive detection

**GPIO Connections:**
```
Pin 2 (PA7) -> Amplifier Output
Pin 3 (PA6) -> Reference/Detection Pin
Pin 4 (PA4) -> Electrode A (via amplifier)
Pin 5 (PA5) -> Electrode B (via amplifier)
GND         -> Amplifier Ground
```

## Recommended Amplifier Circuit

For enhanced sensitivity, use an instrumentation amplifier:
```
INA128 or AD620-based circuit:
- Gain: ~100-1000x
- High input impedance (>1GΩ)
- Low noise, low drift
- Reference pin tied to detection logic
```

## Installation

1. Download the latest `.fap` file from releases
2. Copy to your Flipper Zero's `apps/GPIO/` folder via qFlipper
3. Alternatively, compile from source (see Building section)

## Usage

### Getting Started
1. Connect your electrodes (direct or via amplifier)
2. Launch "Hack the Planet" from the GPIO apps menu
3. The app will automatically:
   - Detect your hardware configuration
   - Calibrate amplifier offset (if detected)
   - Set appropriate sensitivity and thresholds
4. Select "Start Monitoring" to begin

### Monitoring Process
- **Detection Phase**: Hardware auto-detection (~2 seconds)
- **Calibration Phase**: Automatic offset calibration for amplifiers
- **Ready State**: System prepared for monitoring
- **Monitoring State**: Active signal measurement with audio feedback
- **Error Recovery**: Automatic retry on ADC failures

### Electrode Applications

**Plant Monitoring:**
- Large-leafed plants (pothos, philodendron, rubber plants)
- Clean electrodes with alcohol before attachment
- Place on different leaves or stem sections
- Plants may respond to touch, light, music, or environmental changes

**Other Applications:**
- Galvanic skin response (GSR) monitoring
- Electrolyte solution conductivity
- Fruit and vegetable electrical activity
- Educational demonstrations of bioelectricity

## Technical Specifications

### Sampling & Processing
- **Sample Rate**: 20Hz (50ms intervals)
- **ADC Resolution**: 12-bit (4096 levels)
- **Voltage Range**: 0-3.3V reference
- **Buffer Size**: 128 samples for baseline calculation
- **Frequency Range**: 50-2000Hz audio output

### Detection Thresholds
- **Amplified Mode**: 10mV threshold, 10x sensitivity multiplier
- **Direct Mode**: 100mV threshold, 1x sensitivity multiplier
- **Amplifier Detection**: 1.5-1.8V stable signal with <50mV variance

### Error Handling
- Multiple ADC read attempts with retry logic
- Voltage bounds checking and NaN/infinity protection
- Visual and status error indication
- Automatic hardware re-detection on errors

## Building from Source

### Prerequisites
- Flipper Zero official firmware development environment
- Latest Flipper firmware source code

### Compilation
```bash
# In your flipper firmware directory
./fbt fap_hack_the_planet

# Build and launch directly
./fbt launch_app APPID=hack_the_planet

# For debugging
./fbt cli
> log
```

## Troubleshooting

### No Hardware Detected
- Verify GPIO connections match your configuration
- Check amplifier board power and ground connections
- Ensure detection pin (PA6) pulls low for amplifier mode

### Erratic Readings
- Check electrode contact and cleanliness
- Verify a stable power supply to the amplifier
- Avoid touching electrodes during monitoring
- Shield from electromagnetic interference

### Audio Issues
- Ensure the Flipper speaker is enabled
- Check voltage thresholds aren't too high/low
- Verify signal isn't saturating the ADC
- Try adjusting electrode placement

### ADC Errors
- Restart the application to reinitialize ADC
- Check for loose GPIO connections
- Ensure proper grounding of the amplifier circuit
- Use the retry function in the error state

## Scientific Applications

This monitor can be used for legitimate scientific and educational purposes:

### Plant Physiology Research
- Circadian rhythm monitoring
- Environmental response studies
- Ion transport research
- Hydraulic pressure measurements

### Educational Demonstrations
- Bioelectricity concepts
- Signal processing techniques
- Electronic circuit design
- Data acquisition principles

### Citizen Science
- Long-term plant monitoring
- Environmental sensing projects
- Agricultural research applications
- Biofeedback experiments

## Advanced Features

### State Machine Architecture
The application uses a robust state machine for reliable operation:
- **DETECTING**: Hardware configuration detection
- **CALIBRATING**: Amplifier offset calibration
- **READY**: Standby for user input  
- **MONITORING**: Active signal acquisition
- **ERROR**: Error handling and recovery

### Signal Processing
- Running baseline calculation for drift compensation
- Voltage change detection from baseline
- Frequency mapping with bounds checking
- Real-time display scaling and units

## Contributing

We welcome contributions! Priority areas:
- Additional amplifier board support
- Enhanced signal processing algorithms
- Data logging and export capabilities
- Multiple channel monitoring
- Wireless data transmission
- Mobile app integration

## Version History

### v2.0 (Current)
- Automatic hardware detection
- Amplifier board support with calibration
- Enhanced error handling and recovery
- Improved signal processing
- State machine architecture

### v1.0 (Legacy)
- Basic plant monitoring
- Simple and direct electrode connection
- Manual sensitivity adjustment

## License

MIT License - Open source hardware and software encouraged

## Safety & Disclaimers

- **Electrical Safety**: Use only low-voltage circuits (<5V)
- **Plant Care**: Handle plants gently; avoid electrode damage
- **Scientific Interpretation**: Results are for educational purposes
- **No Medical Claims**: Not intended for medical or therapeutic use

---

**Explore the electrical world around you! 🌱⚡🔊**

*From simple plant monitoring to advanced bioelectrical research - Hack the Planet makes it accessible to everyone.*
