#!/usr/bin/env bash
# Validate the installed plugins with auval (AU) and pluginval (AU + VST3).
#
# Usage: scripts/validate.sh [strictness]   (pluginval strictness 1-10, default 10)
#   PLUGINVAL  path to the pluginval binary (default: /Applications/pluginval.app/...)

set -euo pipefail

STRICTNESS="${1:-10}"
PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"
AU="$HOME/Library/Audio/Plug-Ins/Components/Mosaic.component"
VST3="$HOME/Library/Audio/Plug-Ins/VST3/Mosaic.vst3"

# Plugin identity (CLAUDE.md / SPEC section 8). Type is aufx until M1 decides otherwise.
AU_TYPE="aufx"
AU_SUBTYPE="Mosc"
AU_MANUF="Rmsl"

fail=0

if [[ ! -d "$AU" ]];   then echo "Missing $AU (run scripts/build.sh)";   exit 1; fi
if [[ ! -d "$VST3" ]]; then echo "Missing $VST3 (run scripts/build.sh)"; exit 1; fi
if [[ ! -x "$PLUGINVAL" ]]; then echo "pluginval not found at $PLUGINVAL"; exit 1; fi

echo "==> Refreshing the AU component registry"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1

echo "==> auval -v $AU_TYPE $AU_SUBTYPE $AU_MANUF"
if auval -strict -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANUF"; then
    echo "auval: PASS"
else
    echo "auval: FAIL"; fail=1
fi

for plugin in "$AU" "$VST3"; do
    echo "==> pluginval (strictness $STRICTNESS): $plugin"
    if "$PLUGINVAL" --strictness-level "$STRICTNESS" --skip-gui-tests --validate "$plugin"; then
        echo "pluginval: PASS ($plugin)"
    else
        echo "pluginval: FAIL ($plugin)"; fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    echo "==> VALIDATION FAILED"; exit 1
fi
echo "==> All validation passed."
