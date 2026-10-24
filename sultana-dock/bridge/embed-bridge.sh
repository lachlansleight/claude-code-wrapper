#!/bin/bash
# Xcode build phase ("Embed Bridge and Plugin"): puts the bridge inside the
# Sultana app so it can run its own, and the sultana Claude Code plugin so it
# can install it.
#
#   Contents/Helpers/sultana-bridge        Node, stripped, signed with
#                                          sultana-bridge.entitlements
#   Contents/Resources/bridge/bridge.mjs   the bridge as one file (plugin/scripts/bundle.mjs)
#   Contents/Resources/bridge/licenses/    notices for Node and the bundled packages
#   Contents/Resources/ClaudePlugin/       a marketplace holding only sultana-plugin/
#
# Node is SULTANA_NODE if set, else the first Node 20+ on PATH, through nvm,
# or in Homebrew's usual places (Xcode runs build phases with a bare PATH).
# Its architectures become the bridge's: an arm64-only Node makes an app whose
# bridge won't start on Intel Macs. plugin/ needs `npm install` first.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
entitlements="$here/sultana-bridge.entitlements"
runtime="$TARGET_BUILD_DIR/$CONTENTS_FOLDER_PATH/Helpers/sultana-bridge"
resources="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH/bridge"

node_major() { "$1" -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0; }

find_node() {
  if [ -n "${SULTANA_NODE:-}" ]; then
    echo "$SULTANA_NODE"
    return
  fi
  local candidates=()
  if command -v node >/dev/null; then candidates+=("$(command -v node)"); fi
  local nvm_dir="${NVM_DIR:-$HOME/.nvm}"
  if [ -s "$nvm_dir/nvm.sh" ]; then
    candidates+=("$(bash -c ". \"$nvm_dir/nvm.sh\" >/dev/null 2>&1 && nvm which default" 2>/dev/null || true)")
  fi
  candidates+=(/opt/homebrew/bin/node /usr/local/bin/node)
  for candidate in "${candidates[@]}"; do
    if [ -x "$candidate" ] && [ "$(node_major "$candidate")" -ge 20 ]; then
      echo "$candidate"
      return
    fi
  done
  return 1
}

if ! node="$(find_node)"; then
  echo "error: Embed Bridge: no Node.js 20 or newer found. Install it, or set SULTANA_NODE to a node binary." >&2
  exit 1
fi
node="$(realpath "$node")"
if [ "$(node_major "$node")" -lt 20 ]; then
  echo "error: Embed Bridge: $node is older than Node 20." >&2
  exit 1
fi
if [ ! -d "$repo/plugin/node_modules/esbuild" ]; then
  echo "error: Embed Bridge: run 'npm install' in plugin/ first." >&2
  exit 1
fi
echo "Embedding the bridge with Node $("$node" --version) from $node"

archs="$(lipo -archs "$node")"
for arch in $ARCHS; do
  case " $archs " in
    *" $arch "*) ;;
    *) echo "warning: Embed Bridge: $node has no $arch code ($archs), so the built-in bridge won't run on $arch Macs. Set SULTANA_NODE to a universal Node for a universal app." ;;
  esac
done

"$node" "$repo/plugin/scripts/bundle.mjs" "$resources"
node_license="$(dirname "$node")/../LICENSE"
if [ -f "$node_license" ]; then
  cp "$node_license" "$resources/licenses/node.txt"
else
  echo "warning: Embed Bridge: no LICENSE beside $node; the app ships without Node's licence notice."
fi

# The plugin, as a one-plugin marketplace for `claude plugin marketplace add`.
# Its version is the app's plus a hash of the plugin's files, so Claude Code
# (and the app's Settings) can tell an installed copy from an older app.
marketplace="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH/ClaudePlugin"
rm -rf "$marketplace"
mkdir -p "$marketplace/.claude-plugin"
cp -R "$repo/sultana-plugin" "$marketplace/sultana-plugin"
plugin_hash="$(cd "$repo/sultana-plugin" && find . -type f ! -name README.md -print0 | sort -z | xargs -0 shasum | shasum | cut -c1-8)"
"$node" -e '
const fs = require("fs")
const [source, dir, version] = process.argv.slice(1)
const marketplace = JSON.parse(fs.readFileSync(source, "utf8"))
marketplace.plugins = marketplace.plugins.filter((p) => p.name === "sultana")
fs.writeFileSync(`${dir}/.claude-plugin/marketplace.json`, JSON.stringify(marketplace, null, 2) + "\n")
const manifest = `${dir}/sultana-plugin/.claude-plugin/plugin.json`
const plugin = JSON.parse(fs.readFileSync(manifest, "utf8"))
plugin.version = version
fs.writeFileSync(manifest, JSON.stringify(plugin, null, 2) + "\n")
' "$repo/.claude-plugin/marketplace.json" "$marketplace" "${MARKETING_VERSION}+${plugin_hash}"

# Copying, stripping and signing 100 MB takes a few seconds, so only do it
# when Node, the identity or the entitlements change.
identity="${EXPANDED_CODE_SIGN_IDENTITY:--}"
stamp="$DERIVED_FILE_DIR/sultana-bridge.stamp"
want="$node $(stat -f '%z %m' "$node") $identity $(shasum "$entitlements" | cut -d' ' -f1)"
if [ -x "$runtime" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
  exit 0
fi

mkdir -p "$(dirname "$runtime")" "$DERIVED_FILE_DIR"
rm -f "$runtime"
cp "$node" "$runtime"
chmod u+w "$runtime"
codesign --remove-signature "$runtime"
# Local symbols only (114 MB → 90 MB for Node 23); exported ones stay.
strip -x "$runtime"
if [ "${CODE_SIGNING_ALLOWED:-YES}" = YES ]; then
  # Hardened runtime whatever the app's setting, so the entitlements are
  # exercised by every build, not first discovered at notarisation.
  flags=(--force --options runtime --entitlements "$entitlements" --sign "$identity")
  if [ "$identity" != "-" ]; then flags+=(--timestamp); fi
  codesign "${flags[@]}" "$runtime"
fi
echo "$want" >"$stamp"
