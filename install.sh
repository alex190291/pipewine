#!/usr/bin/env bash
# Install the 64-bit PipeWine driver into a user's Wine prefix.
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
prefix=${WINEPREFIX:-$HOME/.wine}
wine_command=${WINELOADER:-wine}
build=true
fix_ableton=true
backup_dir=

usage() {
    cat <<'EOF'
Usage: ./install.sh [OPTIONS]

Build and install 64-bit PipeWine into WINEPREFIX (default: ~/.wine).
Run as your normal user. No system Wine or Proton files are modified.

  --prefix PATH       Wine prefix to install into
  --wine PATH         Wine loader to use (e.g. a custom runtime's wine64)
  --no-build          Install existing build64 artifacts
  --no-ableton        Skip updating existing Ableton launch-ableton.sh scripts
  -h, --help          Show this help

The installer registers the driver, saves backups in the prefix, and creates
<prefix>/pipewine-run for launching applications with the driver search path.
Existing Ableton launchers under drive_c/ProgramData/Ableton are updated to use
this wrapper. Other launchers should use pipewine-run instead of wine.
EOF
}

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

while (($#)); do
    case $1 in
        --prefix|--wine)
            (($# >= 2)) && [[ -n $2 ]] || die "$1 requires a value"
            if [[ $1 == --prefix ]]; then prefix=$2; else wine_command=$2; fi
            shift 2 ;;
        --prefix=*) prefix=${1#*=}; [[ -n $prefix ]] || die 'Empty prefix'; shift ;;
        --wine=*) wine_command=${1#*=}; [[ -n $wine_command ]] || die 'Empty Wine loader'; shift ;;
        --no-build) build=false; shift ;;
        --no-ableton) fix_ableton=false; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1 (use --help)" ;;
    esac
done

((EUID != 0)) || die 'Run without sudo, as the user who owns the Wine prefix.'
for tool in timeout install mktemp realpath; do
    command -v "$tool" >/dev/null || die "Required tool missing: $tool"
done
wine_binary=$(command -v -- "$wine_command") || die "Wine loader not found: $wine_command"
wine_binary=$(realpath -- "$wine_binary")
[[ -x $wine_binary ]] || die "Wine loader is not executable: $wine_binary"
if [[ -f $prefix/system.reg ]] && [[ $(head -n 5 "$prefix/system.reg") == *'#arch=win32'* ]]; then
    die 'This installer requires a 64-bit Wine prefix.'
fi
[[ ${WINEARCH:-win64} != win32 ]] || die 'Unset WINEARCH=win32 or select a 64-bit prefix.'

if $build; then
    command -v make >/dev/null || die 'make is required to build the driver'
    make -C "$script_dir" 64
fi
artifacts=(pipewine64.dll pipewine64.dll.so libpwasio_gui.so)
for artifact in "${artifacts[@]}"; do
    [[ -s $script_dir/build64/$artifact ]] || die "Missing build64/$artifact; run make 64."
done

mkdir -p -- "$prefix"
prefix=$(cd -- "$prefix" && pwd -P)
export WINEPREFIX="$prefix"
driver_dir="$prefix/drive_c/windows/system32"
export WINEDLLPATH="$driver_dir${WINEDLLPATH:+:$WINEDLLPATH}"
if [[ ! -f $prefix/system.reg || ! -d $driver_dir ]]; then
    printf 'Initializing Wine prefix: %s\n' "$prefix"
    timeout 60s "$wine_binary" wineboot -u
fi
[[ -d $driver_dir ]] || die 'Wine did not create windows/system32.'

mkdir -p -- "$prefix/.pipewine-backups"
backup_dir=$(mktemp -d "$prefix/.pipewine-backups/install-XXXXXXXX")
printf 'Installing into: %s\nBackups: %s\n' "$prefix" "$backup_dir"
trap 'printf "Installation failed; backups are in %s\n" "$backup_dir" >&2' ERR

backup_file() {
    local relative=${1#"$prefix/"}
    if [[ -e $1 || -L $1 ]]; then
        mkdir -p -- "$backup_dir/$(dirname -- "$relative")"
        cp -a -- "$1" "$backup_dir/$relative"
    fi
}

replace_file() {
    local source=$1 destination=$2 mode=$3 staged
    backup_file "$destination"
    staged=$(mktemp "$destination.pipewine-XXXXXXXX")
    install -m "$mode" -- "$source" "$staged"
    mv -f -- "$staged" "$destination"
    cmp -- "$source" "$destination"
}

for registry in system.reg user.reg userdef.reg; do
    backup_file "$prefix/$registry"
done
for artifact in "${artifacts[@]}"; do
    replace_file "$script_dir/build64/$artifact" "$driver_dir/$artifact" 644
done

printf 'Registering the 64-bit driver...\n'
timeout 45s "$wine_binary" 'C:\windows\system32\regsvr32.exe' /s pipewine64.dll
timeout 30s "$wine_binary" 'C:\windows\system32\reg.exe' query 'HKLM\Software\ASIO\PipeWine' /reg:64
timeout 30s "$wine_binary" 'C:\windows\system32\reg.exe' query 'HKCR\CLSID\{A4262EE4-C528-4FF9-87BE-56261AD792C3}\InprocServer32' /reg:64

# Keep the selected runtime and prefix together without changing system Wine.
runner="$prefix/pipewine-run"
runner_source="$backup_dir/pipewine-run.new"
{
    cat <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
prefix_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
export WINEPREFIX="$prefix_dir"
driver_dir="$prefix_dir/drive_c/windows/system32"
if [[ ${WINEDLLPATH:-} != "$driver_dir" && ${WINEDLLPATH:-} != "$driver_dir":* ]]; then
    export WINEDLLPATH="$driver_dir${WINEDLLPATH:+:$WINEDLLPATH}"
fi
EOF
    printf 'exec %q "$@"\n' "$wine_binary"
} > "$runner_source"
replace_file "$runner_source" "$runner" 755

if $fix_ableton; then
    shopt -s nullglob
    for launcher in "$prefix"/drive_c/ProgramData/Ableton/*/Program/launch-ableton.sh; do
        patched="$backup_dir/launch-ableton.new"
        changed=false
        {
            while IFS= read -r line || [[ -n $line ]]; do
                if [[ $line =~ ^([[:space:]]*)exec[[:space:]]+(wine|wine64)[[:space:]]+(.*)$ ]]; then
                    printf '%sexec %q %s\n' "${BASH_REMATCH[1]}" "$runner" "${BASH_REMATCH[3]}"
                    changed=true
                else
                    printf '%s\n' "$line"
                fi
            done < "$launcher"
        } > "$patched"
        if $changed; then
            bash -n "$patched"
            replace_file "$patched" "$launcher" 755
            printf 'Updated Ableton launcher: %s\n' "$launcher"
        fi
    done
fi

printf '\nPipeWine installed and registered successfully.\n'
printf 'Restart Ableton using its updated shortcut, or launch other applications with:\n'
printf '  %q /path/to/application.exe\n' "$runner"
