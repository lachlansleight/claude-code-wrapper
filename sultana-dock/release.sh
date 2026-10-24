#!/bin/bash
# Build a release of the Sultana app for other people's Macs: signed with a
# Developer ID and hardened runtime, notarised and stapled, inside a DMG
# that is signed, notarised and stapled too.
#
#   sultana-dock/release.sh                 build sultana-dock/build/release/Sultana-<version>.dmg
#   sultana-dock/release.sh --no-notarize   everything but notarisation (signing checks only)
#   sultana-dock/release.sh --allow-dirty   build from uncommitted changes
#
# Needs, once:
#   - a "Developer ID Application" certificate in the login keychain
#     (Xcode ▸ Settings ▸ Accounts ▸ Manage Certificates ▸ + ▸ Developer ID Application);
#   - notarisation credentials stored in the keychain:
#       xcrun notarytool store-credentials sultana-notary --apple-id <email> --team-id <team>
#
# SULTANA_IDENTITY picks a certificate by name if there's more than one ("-"
# signs ad hoc, for trying the script out; implies --no-notarize).
# SULTANA_NOTARY_PROFILE names the stored credentials (default sultana-notary).
# The version is MARKETING_VERSION in the Xcode project; the build number is
# the commit count, so it always goes up.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
profile="${SULTANA_NOTARY_PROFILE:-sultana-notary}"
out="$here/build/release"
notarize=1
allow_dirty=0
while [ $# -gt 0 ]; do
  case "$1" in
    --no-notarize) notarize=0; shift ;;
    --allow-dirty) allow_dirty=1; shift ;;
    -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

fail() { echo "error: $*" >&2; exit 1; }

if [ "$allow_dirty" = 0 ] && [ -n "$(git -C "$repo" status --porcelain)" ]; then
  fail "uncommitted changes; commit them, or pass --allow-dirty"
fi

# The signing identity, and the team it belongs to (its certificate's OU).
if [ "${SULTANA_IDENTITY:-}" = "-" ]; then
  identity="-"
  team=""
  notarize=0
else
  identities="$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | sort -u)"
  if [ -n "${SULTANA_IDENTITY:-}" ]; then
    identities="$(printf '%s\n' "$identities" | grep -F "$SULTANA_IDENTITY" || true)"
  fi
  case "$(printf '%s' "$identities" | grep -c . || true)" in
    0) fail "no Developer ID Application certificate in the keychain (see the top of $0)" ;;
    1) identity="$identities" ;;
    *) fail "several Developer ID certificates; pick one with SULTANA_IDENTITY:
$identities" ;;
  esac
  team="$(security find-certificate -c "$identity" -p | openssl x509 -noout -subject | sed -n 's/.*OU *= *\([A-Z0-9]*\).*/\1/p')"
  [ -n "$team" ] || fail "couldn't read the team ID from \"$identity\""
fi

if [ "$notarize" = 1 ] && ! xcrun notarytool history --keychain-profile "$profile" >/dev/null 2>&1; then
  fail "no notarisation credentials under \"$profile\" (see the top of $0), or no network"
fi

echo "Signing as: $identity${team:+ (team $team)}"
SULTANA_NODE="$("$here/bridge/fetch-node.sh")"
export SULTANA_NODE

echo "Building (Release)…"
dd="$here/build/release-dd"
log="$out/xcodebuild.log"
mkdir -p "$out"
signing=(CODE_SIGN_STYLE=Manual "CODE_SIGN_IDENTITY=$identity" "DEVELOPMENT_TEAM=$team")
if [ "$identity" != "-" ]; then signing+=(OTHER_CODE_SIGN_FLAGS=--timestamp); fi
if ! xcodebuild -project "$here/SultanaDock.xcodeproj" -scheme Sultana -configuration Release \
    -derivedDataPath "$dd" "${signing[@]}" ENABLE_HARDENED_RUNTIME=YES \
    "CURRENT_PROJECT_VERSION=$(git -C "$repo" rev-list --count HEAD)" clean build >"$log" 2>&1; then
  grep -E ': error: ' "$log" | sort -u >&2 || true
  fail "build failed; full log: $log"
fi
grep -E 'warning: Embed Bridge' "$log" | sort -u >&2 || true

app="$dd/Build/Products/Release/Sultana.app"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
build="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$app/Contents/Info.plist")"
echo "Built Sultana $version ($build)"
codesign --verify --deep --strict "$app" || fail "the app's signature doesn't verify"

# Upload to Apple's notary service and wait (usually a few minutes); print
# Apple's log if it's turned down.
notarize() {
  echo "Notarising $(basename "$1")…"
  local result id status
  result="$(xcrun notarytool submit "$1" --keychain-profile "$profile" --wait --output-format json)"
  id="$(printf '%s' "$result" | plutil -extract id raw -o - -)"
  status="$(printf '%s' "$result" | plutil -extract status raw -o - -)"
  if [ "$status" != "Accepted" ]; then
    xcrun notarytool log "$id" --keychain-profile "$profile" >&2 || true
    fail "notarisation of $(basename "$1") came back \"$status\" (submission $id)"
  fi
}

if [ "$notarize" = 1 ]; then
  zip="$out/Sultana-$version.zip"
  ditto -c -k --keepParent "$app" "$zip"
  notarize "$zip"
  rm -f "$zip"
  xcrun stapler staple -q "$app"
fi

echo "Making the disk image…"
dmg="$out/Sultana-$version.dmg"
staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
ditto "$app" "$staging/Sultana.app"
ln -s /Applications "$staging/Applications"
rm -f "$dmg"
hdiutil create -quiet -volname "Sultana $version" -srcfolder "$staging" -fs HFS+ -format UDZO "$dmg"
if [ "$identity" = "-" ]; then
  codesign --sign - "$dmg"
else
  codesign --sign "$identity" --timestamp "$dmg"
fi

if [ "$notarize" = 1 ]; then
  notarize "$dmg"
  xcrun stapler staple -q "$dmg"
  spctl --assess --type open --context context:primary-signature "$dmg" || fail "Gatekeeper rejects the disk image"
  spctl --assess --type execute "$app" || fail "Gatekeeper rejects the app"
fi

(cd "$out" && shasum -a 256 "$(basename "$dmg")" >"$(basename "$dmg").sha256")
echo "Ready: $dmg ($(du -h "$dmg" | cut -f1 | tr -d ' '))"
if [ "$notarize" = 0 ]; then
  echo "Not notarised: on other Macs it opens only through Privacy & Security ▸ Open Anyway (docs/INSTALL.md)."
fi
