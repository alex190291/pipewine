#!/bin/bash
# PipeWine Installation Script
# Streamlined installer for PipeWire ASIO driver

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr}"
WINE_ARCH="${WINE_ARCH:-both}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    log_info "Checking dependencies..."
    
    # Check for PipeWire
    if ! pkg-config --exists libpipewire-0.3; then
        log_error "PipeWire development libraries not found. Please install libpipewire-0.3-dev"
        exit 1
    fi
    
    # Check for Wine
    if ! command -v wine &> /dev/null; then
        log_error "Wine not found. Please install Wine"
        exit 1
    fi
    
    # Check for build tools
    if ! command -v gcc &> /dev/null; then
        log_error "GCC not found. Please install build-essential"
        exit 1
    fi
    
    if ! command -v winebuild &> /dev/null; then
        log_error "Wine development tools not found. Please install wine-dev"
        exit 1
    fi
    
    log_info "All dependencies satisfied"
}

detect_wine_paths() {
    log_info "Detecting Wine installation paths..."
    
    # Common Wine library paths
    WINE_PATHS=(
        "/usr/lib/x86_64-linux-gnu/wine"
        "/usr/lib/i386-linux-gnu/wine"
        "/usr/lib/wine"
        "/opt/wine-stable/lib/wine"
        "/opt/wine-staging/lib/wine"
    )
    
    WINE_LIB_PATH=""
    for path in "${WINE_PATHS[@]}"; do
        if [ -d "$path" ]; then
            WINE_LIB_PATH="$path"
            break
        fi
    done
    
    if [ -z "$WINE_LIB_PATH" ]; then
        log_error "Could not detect Wine library path"
        exit 1
    fi
    
    log_info "Using Wine library path: $WINE_LIB_PATH"
}

build_driver() {
    log_info "Building PipeWine driver..."
    
    cd "$SCRIPT_DIR"
    
    case "$WINE_ARCH" in
        "32")
            make clean
            make 32
            ;;
        "64")
            make clean
            make 64
            ;;
        "both")
            make clean
            make 32
            make 64
            ;;
        *)
            log_error "Invalid architecture: $WINE_ARCH. Use 32, 64, or both"
            exit 1
            ;;
    esac
    
    log_info "Build completed successfully"
}

install_driver() {
    log_info "Installing PipeWine driver..."
    
    # Create configuration directory
    mkdir -p /etc/pipewine
    
    # Install configuration file
    if [ -f "pipewine.conf" ]; then
        cp pipewine.conf /etc/pipewine/
        log_info "Installed system configuration file"
    fi
    
    # Install driver files
    case "$WINE_ARCH" in
        "32"|"both")
            if [ -d "build32" ]; then
                install -m 644 build32/pipewine32.dll "$WINE_LIB_PATH/i386-windows/" 2>/dev/null || true
                install -m 644 build32/pipewine32.dll.so "$WINE_LIB_PATH/i386-unix/" 2>/dev/null || true
                install -m 644 build32/libpwasio_gui.so "$WINE_LIB_PATH/i386-unix/" 2>/dev/null || true
                log_info "Installed 32-bit driver"
            fi
            ;;
    esac
    
    case "$WINE_ARCH" in
        "64"|"both")
            if [ -d "build64" ]; then
                install -m 644 build64/pipewine64.dll "$WINE_LIB_PATH/x86_64-windows/" 2>/dev/null || true
                install -m 644 build64/pipewine64.dll.so "$WINE_LIB_PATH/x86_64-unix/" 2>/dev/null || true
                install -m 644 build64/libpwasio_gui.so "$WINE_LIB_PATH/x86_64-unix/" 2>/dev/null || true
                log_info "Installed 64-bit driver"
            fi
            ;;
    esac
}

register_driver() {
    log_info "Registering PipeWine driver..."
    
    # Use the existing registration script if available
    if [ -f "wineasio-register" ]; then
        chmod +x wineasio-register
        ./wineasio-register
        log_info "Driver registered successfully"
    else
        log_warn "Registration script not found. You may need to register manually."
    fi
}

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --arch=ARCH     Architecture to build (32, 64, both) [default: both]"
    echo "  --prefix=PATH   Installation prefix [default: /usr]"
    echo "  --no-register   Skip driver registration"
    echo "  --help          Show this help message"
}

main() {
    local no_register=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --arch=*)
                WINE_ARCH="${1#*=}"
                shift
                ;;
            --prefix=*)
                PREFIX="${1#*=}"
                shift
                ;;
            --no-register)
                no_register=true
                shift
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    log_info "Starting PipeWine installation..."
    log_info "Architecture: $WINE_ARCH"
    log_info "Prefix: $PREFIX"
    
    check_dependencies
    detect_wine_paths
    build_driver
    install_driver
    
    if [ "$no_register" = false ]; then
        register_driver
    fi
    
    log_info "PipeWine installation completed successfully!"
    log_info "You can now use PipeWine in your Wine applications."
    log_info "Configuration file: /etc/pipewine/pipewine.conf"
}

main "$@" 