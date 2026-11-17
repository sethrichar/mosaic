#!/usr/bin/env bash
# Validate the installed plugins with auval (AU) and pluginval (AU + VST3).
#
# Usage: scripts/validate.sh [strictness]   (pluginval strictness 1-10, default 10)
#   PLUGINVAL  path to the pluginval binary (default: /Applications/pluginval.app/...)
#   MOSAIC_VALIDATE_INST_PROBE=0 to skip the M1 instrument-variant probe

set -euo pipefail

STRICTNESS="${1:-10}"
PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"
COMPONENTS="$HOME/Library/Audio/Plug-Ins/Components"
VST3S="$HOME/Library/Audio/Plug-Ins/VST3"

# name | AU type | AU subtype | manufacturer   (CLAUDE.md / SPEC section 8)
PLUGINS=(
    "Mosaic|aufx|Mosc|Rmsl"
)
if [[ "${MOSAIC_VALIDATE_INST_PROBE:-1}" == "1" ]]; then
    PLUGINS+=("Mosaic Inst Probe|aumu|MosI|Rmsl")
fi

if [[ ! -x "$PLUGINVAL" ]]; then echo "pluginval not found at $PLUGINVAL"; exit 1; fi

echo "==> Refreshing the AU component registry"
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 1

fail=0
for entry in "${PLUGINS[@]}"; do
    IFS='|' read -r name autype subtype manuf <<< "$entry"
    AU="$COMPONENTS/$name.component"
    VST3="$VST3S/$name.vst3"
    if [[ ! -d "$AU" ]];   then echo "Missing $AU (run scripts/build.sh)";   fail=1; continue; fi
    if [[ ! -d "$VST3" ]]; then echo "Missing $VST3 (run scripts/build.sh)"; fail=1; continue; fi

    echo "==> auval -strict -v $autype $subtype $manuf   ($name)"
    if auval -strict -v "$autype" "$subtype" "$manuf"; then
        echo "auval: PASS ($name)"
    else
        echo "auval: FAIL ($name)"; fail=1
    fi

    for plugin in "$AU" "$VST3"; do
        echo "==> pluginval (strictness $STRICTNESS): $plugin"
        if "$PLUGINVAL" --strictness-level "$STRICTNESS" --skip-gui-tests --validate "$plugin"; then
            echo "pluginval: PASS ($plugin)"
        else
            echo "pluginval: FAIL ($plugin)"; fail=1
        fi
    done
done

if [[ $fail -ne 0 ]]; then
    echo "==> VALIDATION FAILED"; exit 1
fi
echo "==> All validation passed."
