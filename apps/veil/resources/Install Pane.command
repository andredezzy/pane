#!/bin/bash

clear

echo "==================================="
echo "          Install Pane"
echo "==================================="
echo ""

APP_NAME="Pane"
DMG_APP="$(dirname "$0")/$APP_NAME.app"
INSTALL_APP="/Applications/$APP_NAME.app"

if [ ! -d "$DMG_APP" ]; then
    echo "[Error] $APP_NAME.app not found next to this installer."
    echo "        Make sure to run this from inside the disk image."
    echo ""
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
fi

if pgrep -x "$APP_NAME" > /dev/null 2>&1; then
    echo "Closing $APP_NAME..."
    osascript -e "tell application \"$APP_NAME\" to quit" 2>/dev/null
    sleep 1
fi

if [ -d "$INSTALL_APP" ]; then
    echo "Removing previous version..."
    rm -rf "$INSTALL_APP"
fi

echo "Copying to Applications..."
cp -R "$DMG_APP" "/Applications/"

echo "Removing quarantine flag..."
xattr -rd com.apple.quarantine "$INSTALL_APP" 2>/dev/null

echo "Launching $APP_NAME..."
open "$INSTALL_APP"

echo ""
echo "Done! You can close this window."
