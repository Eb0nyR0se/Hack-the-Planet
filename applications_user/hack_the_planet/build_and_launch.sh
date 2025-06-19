#!/bin/bash
# Build and launch your Flipper Zero app

# Config
APP_NAME="hack_the_planet"
FIRMWARE_DIR="flipperzero-firmware"

# Navigate to firmware root (adjust path if needed)
if [ ! -d "$FIRMWARE_DIR" ]; then
    echo "Firmware directory '$FIRMWARE_DIR' not found!"
    exit 1
fi

cd "$FIRMWARE_DIR" || exit 1

# Build the app
echo "==> Building app: $APP_NAME"
./fbt fap_$APP_NAME || {
    echo "❌ Build failed!"
    exit 1
}

# Launch the app
echo "==> Launching app: $APP_NAME"
./fbt launch_app APPID=$APP_NAME || {
    echo "❌ Launch failed!"
    exit 1
}
