#!/bin/bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)"
   exit 1
fi

echo "Installing Cascoin Core Linux Distribution..."

# Create installation directory
mkdir -p /opt/cascoin

# Copy all files to /opt/cascoin
cp -r bin lib /opt/cascoin/

# Create symlinks in /usr/local/bin
ln -sf /opt/cascoin/bin/cascoin-qt-wrapper /usr/local/bin/cascoin-qt
ln -sf /opt/cascoin/bin/cascoind-wrapper /usr/local/bin/cascoind
ln -sf /opt/cascoin/bin/cascoin-cli /usr/local/bin/cascoin-cli
ln -sf /opt/cascoin/bin/cascoin-tx /usr/local/bin/cascoin-tx



echo "Installation complete!"
echo ""
echo "You can now run:"
echo "  cascoin-qt    # GUI wallet (requires Qt6)"
echo "  cascoind      # Daemon"
echo "  cascoin-cli   # CLI tool"
echo "  cascoin-tx    # Transaction tool"
