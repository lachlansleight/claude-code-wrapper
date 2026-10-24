#!/bin/bash
# Downloads the Node.js release pinned in node.env (macOS arm64) into
# sultana-dock/build/node/, checks it against the pinned SHA-256, unpacks it,
# and prints the path of its node binary. Does nothing but print the path if
# it's already there.
#
#   SULTANA_NODE="$(sultana-dock/bridge/fetch-node.sh)"
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=node.env
. "$here/node.env"
cache="$(cd "$here/.." && pwd)/build/node"
name="node-v$NODE_VERSION-darwin-arm64"
binary="$cache/$name/bin/node"

if [ -x "$binary" ]; then
  echo "$binary"
  exit 0
fi

mkdir -p "$cache"
tarball="$cache/$name.tar.xz"
echo "Downloading Node $NODE_VERSION (macOS arm64)…" >&2
curl -fsSL -o "$tarball.partial" "https://nodejs.org/dist/v$NODE_VERSION/$name.tar.xz"
actual="$(shasum -a 256 "$tarball.partial" | cut -d' ' -f1)"
if [ "$actual" != "$NODE_SHA256" ]; then
  rm -f "$tarball.partial"
  echo "error: $name.tar.xz has SHA-256 $actual, expected $NODE_SHA256 (node.env)" >&2
  exit 1
fi
mv "$tarball.partial" "$tarball"
tar -xJf "$tarball" -C "$cache" "$name/bin/node" "$name/LICENSE"
rm -f "$tarball"
echo "$binary"
