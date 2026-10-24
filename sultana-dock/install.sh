#!/bin/bash
# Build Sultana Dock (Release) and install it as a normal app, so it runs
# without Xcode open.
#
#   sultana-dock/install.sh              # build, install to /Applications, open it
#   sultana-dock/install.sh --dest DIR   # install somewhere else
#   sultana-dock/install.sh --no-open    # don't launch it afterwards
#
# Needs Xcode installed (for xcodebuild), not running. The build goes to
# sultana-dock/build/ (gitignored).

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
dest="/Applications"
open_after=1
while [ $# -gt 0 ]; do
  case "$1" in
    --dest) dest="$2"; shift 2 ;;
    --no-open) open_after=0; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

app_name="Sultana Dock.app"
built="$here/build/Build/Products/Release/$app_name"
installed="$dest/$app_name"

echo "Building $app_name (Release)…"
log="$here/build/xcodebuild.log"
mkdir -p "$here/build"
if ! xcodebuild -project "$here/SultanaDock.xcodeproj" -scheme "Sultana Dock" -configuration Release \
    -derivedDataPath "$here/build" build >"$log" 2>&1; then
  grep -E ': error: ' "$log" | sort -u >&2 || true
  echo "Build failed; full log: $log" >&2
  exit 1
fi

# Quit a copy already running from the install location before replacing it.
if pgrep -f "$installed/Contents/MacOS/" >/dev/null; then
  echo "Quitting the running copy in $dest…"
  pkill -f "$installed/Contents/MacOS/" || true
  sleep 1
fi

mkdir -p "$dest"
rm -rf "$installed"
ditto "$built" "$installed"
echo "Installed: $installed"

if [ "$open_after" = 1 ]; then
  open "$installed"
fi
