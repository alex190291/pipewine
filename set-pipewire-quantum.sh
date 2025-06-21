#!/bin/bash

# PipeWire ASIO Quantum Setting Script
# This script sets the PipeWire quantum to match your ASIO buffer size for optimal performance

QUANTUM=${1:-256}

echo "Setting PipeWire quantum to $QUANTUM samples..."

# Check if pw-metadata is available
if ! command -v pw-metadata &> /dev/null; then
    echo "Error: pw-metadata command not found. Please install pipewire-utils package:"
    echo "  Fedora: sudo dnf install pipewire-utils"
    echo "  Ubuntu: sudo apt install pipewire-utils"
    exit 1
fi

# Set the quantum
if pw-metadata -n settings 0 clock.quantum $QUANTUM; then
    echo "Successfully set PipeWire quantum to $QUANTUM samples"
    echo "This should match your ASIO buffer size for optimal performance"
    
    # Show current quantum
    echo ""
    echo "Current PipeWire settings:"
    pw-metadata -n settings | grep quantum || echo "No quantum setting found"
else
    echo "Failed to set PipeWire quantum"
    exit 1
fi

echo ""
echo "Usage: $0 [buffer_size]"
echo "Example: $0 128    # Set quantum to 128 samples"
echo "Example: $0 256    # Set quantum to 256 samples"
echo "Example: $0 512    # Set quantum to 512 samples" 