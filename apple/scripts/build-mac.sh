#!/bin/bash
# Builds the Mac app (Mac Catalyst), signs it with Developer ID, notarizes it and packs it in a
# .dmg that installs without the App Store. Run on a Mac with Xcode, from the repository root:
#
#   TEAM_ID=ABCDE12345 NOTARY_PROFILE=imago-notary apple/scripts/build-mac.sh
#
# Once per Mac: a "Developer ID Application" certificate in the keychain (Xcode → Settings →
# Accounts → Manage Certificates → + → Developer ID Application), and the notary credentials
# stored under NOTARY_PROFILE:
#
#   xcrun notarytool store-credentials imago-notary --apple-id you@imagoaero.com \
#       --team-id ABCDE12345 --password <app-specific password from account.apple.com>
#
# Output: build/mac/ImagoPayload-<version>.dmg, notarized and stapled.
set -euo pipefail

: "${TEAM_ID:?Set TEAM_ID to the Apple Developer team ID}"
: "${NOTARY_PROFILE:?Set NOTARY_PROFILE to the notarytool keychain profile}"

root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/mac"
archive="$out/ImagoPayload.xcarchive"
rm -rf "$out"
mkdir -p "$out"

cd "$root/apple"
xcodegen

xcodebuild -project ImagoPayloadControl.xcodeproj -scheme ImagoPayloadControl \
    -destination 'generic/platform=macOS,variant=Mac Catalyst' -configuration Release \
    -archivePath "$archive" DEVELOPMENT_TEAM="$TEAM_ID" archive

cat > "$out/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key><string>developer-id</string>
    <key>teamID</key><string>$TEAM_ID</string>
    <key>signingStyle</key><string>automatic</string>
</dict>
</plist>
EOF
xcodebuild -exportArchive -archivePath "$archive" -exportPath "$out/export" \
    -exportOptionsPlist "$out/ExportOptions.plist"

app="$(find "$out/export" -maxdepth 1 -name '*.app' | head -1)"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
dmg="$out/ImagoPayload-$version.dmg"

# The .dmg carries the app and a link to /Applications to drag it to.
staging="$out/dmg"
mkdir -p "$staging"
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
hdiutil create -volname "Imago Payload" -srcfolder "$staging" -fs HFS+ -format UDZO "$dmg"

identity="$(security find-identity -v -p codesigning | grep "Developer ID Application" \
    | grep "($TEAM_ID)" | head -1 | sed -E 's/.*"(.*)"/\1/')"
codesign --sign "$identity" --timestamp "$dmg"

# Notarizing the .dmg covers the app inside; stapling lets it open offline.
xcrun notarytool submit "$dmg" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$dmg"
spctl --assess --type open --context context:primary-signature --verbose "$dmg"

echo "Done: $dmg"
