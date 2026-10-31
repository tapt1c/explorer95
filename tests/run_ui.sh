#!/bin/sh
# Run the existing smoke targets in an isolated, fully managed X display.
set -eu
cd "$(dirname "$0")/.."
: "${WINEPREFIX:?Set WINEPREFIX to a disposable Wine prefix}"
# The Unix filesystem must represent Windows ANSI/OEM names losslessly.
# Debian's default C locale otherwise makes Wine fail non-ASCII CreateFileA.
export LANG=C.UTF-8 LC_ALL=C.UTF-8
# Wine's Unix Recycle Bin uses XDG data outside the prefix by default.
export XDG_DATA_HOME="$WINEPREFIX/xdg-data"
mkdir -p "$XDG_DATA_HOME"
mkdir -p build
exec xvfb-run -a -s "-screen 0 ${EX95_TEST_SCREEN:-640x480x24}" sh -eu -c '
    openbox --sm-disable --config-file tests/openbox.xml > build/openbox.log 2>&1 &
    wm_pid=$!
    trap "kill $wm_pid 2>/dev/null || true" EXIT
    ready=0
    for attempt in $(seq 1 50); do
        if obxprop --root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "WINDOW.*="; then
            ready=1
            break
        fi
        sleep 0.1
    done
    if [ "$ready" -ne 1 ]; then
        cat build/openbox.log
        exit 1
    fi
    make -j2 ui64-smoke "$@"
' ex95-ui "$@"
