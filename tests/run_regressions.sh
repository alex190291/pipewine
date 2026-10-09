#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
make 64
make -C tests all test_helper_regressions test_driver_regressions.exe test_driver_live.exe test_gui_regressions
./tests/test_helper_regressions
run_dir=$(mktemp -d /tmp/pipewine-regressions.XXXXXX)
mkdir -p "$run_dir/prefix" "$run_dir/runtime"
chmod 700 "$run_dir/runtime"
server_pid=
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    # Keep Wine prefix and logs for inspecting failures.
    printf 'Regression artifacts: %s\n' "$run_dir"
}
trap cleanup EXIT
export WINEPREFIX="$run_dir/prefix" WINEDEBUG=-all
export WINEDLLOVERRIDES='winemenubuilder.exe=d'
export PIPEWIRE_RUNTIME_DIR="$run_dir/runtime" XDG_RUNTIME_DIR="$run_dir/runtime"
export XDG_CONFIG_HOME="$run_dir/config"
QT_QPA_PLATFORM=offscreen ./tests/test_gui_regressions
export PWASIO_NUMBER_INPUTS=4 PWASIO_NUMBER_OUTPUTS=4
export PWASIO_NUMBER_MIDI_INPUTS=0 PWASIO_NUMBER_MIDI_OUTPUTS=0
export PWASIO_CONNECT_TO_HARDWARE=false
# Force the headless Wine driver so test runs do not open windows.
export DISPLAY= WAYLAND_DISPLAY=
timeout 60s tests/test_driver_regressions.exe
pipewire > "$run_dir/pipewire.log" 2>&1 &
server_pid=$!
for ((i=0; i<100; i++)); do
    [[ -S "$run_dir/runtime/pipewire-0" ]] && break
    sleep 0.05
done
if [[ ! -S "$run_dir/runtime/pipewire-0" ]]; then
    cat "$run_dir/pipewire.log"
    exit 1
fi
timeout 45s tests/test_driver_live.exe

# Exercise actual COM loading/reuse too. Only this temporary prefix is changed.
export WINEDLLPATH="$PWD/build64"
cp build64/pipewine64.dll build64/pipewine64.dll.so "$WINEPREFIX/drive_c/windows/system32/"
timeout 30s wine regsvr32 /s "$PWD/build64/pipewine64.dll"
export PWASIO_NUMBER_MIDI_INPUTS=1 PWASIO_NUMBER_MIDI_OUTPUTS=1
for test in test_asio_minimal test_buffer test_dsp test_integration test_midi; do
    timeout 40s "tests/$test.exe"
done
