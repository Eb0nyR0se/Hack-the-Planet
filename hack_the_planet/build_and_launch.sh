#!/bin/bash
# Build and launch your Flipper Zero app

# Navigate to firmware root (adjust path if needed)
cd flipperzero-firmware || exit 1

# Build the app
./fbt fap_hack_the_planet || exit 1

# Launch the app
./fbt launch_app APPID=hack_the_planet
