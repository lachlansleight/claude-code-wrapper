# Installing Sultana

Sultana is a little robot face that shows what Claude Code is doing. On a Mac
it lives in your Dock, and can sit on your desktop. If you have the physical
robot, the app is what it connects to.

You need:
- a Mac with Apple silicon (M1 or later), on macOS 14 Sonoma or newer;
- [Claude Code](https://claude.com/claude-code), installed and working in a
  terminal.

## Install

1. Download `Sultana-<version>.dmg` from the
   [latest release](https://github.com/lachlansleight/claude-code-wrapper/releases/latest).
2. Open it and drag **Sultana** onto **Applications**. Eject the disk image.
3. Open Sultana from your Applications folder. macOS will refuse the first
   time (see below): that's expected. Once it opens, its Settings window
   shows the **Setup** tab.
4. Under **Claude Code**, click **Install Plugin**.
5. Start a new Claude Code session, or run `/reload-plugins` in one that's
   already open. Type anything: the face in the Dock starts working, and
   Settings shows "Last hook from Claude Code … ago".

Optional, also on the Setup tab:
- **Open at login**, so Sultana is always there.
- **Show the robot on the desktop**: the whole robot, arms and all, floating
  on your desktop. Drag it to move it; pinch it, or right-click it, to
  resize it.
- **Robot**, if you have the physical robot: the steps and the details to
  type into its setup page. It says when the robot has connected.

The **General** tab has the arm colour, keeping the robot on top, and when
the Dock icon bounces.

### "Apple could not verify Sultana…"

Sultana isn't signed or notarised by Apple yet, so the first time you open it
macOS says it can't check it for malware, and won't open it. To open it
anyway:

1. Click **Done** on that message (not Move to Bin).
2. Open **System Settings ▸ Privacy & Security**, and scroll down to
   **Security**. Next to "Sultana was blocked to protect your Mac", click
   **Open Anyway**.
3. Confirm with your password or Touch ID, then click **Open Anyway** again
   when asked.

You only do this once per version. (If you're comfortable in Terminal,
`xattr -dr com.apple.quarantine /Applications/Sultana.app` does the same
thing.)

## If something's not right

- **"Couldn't find the claude command"**: install Claude Code first, or run
  the two commands Settings shows in a terminal.
- **"Every hook will arrive twice"**: an older Sultana setup is still sending
  hooks. Follow the message in Settings to remove it.
- **Nothing happens in Claude Code**: plugins load when a session starts.
  Start a new session, or run `/reload-plugins`.
- **Settings ▸ Bridge says port 8787 is in use**: something else, maybe
  another copy of the bridge, is using it. Quit that, and Sultana takes over
  within seconds.
- **The robot won't connect**: turn on *Let robots on my network connect*,
  and check the robot is on the same WiFi as the Mac. With the macOS firewall
  on, allow `sultana-bridge` to accept incoming connections when asked.

## Updating

Download the new release and replace the app in Applications, then open it
the same way as the first time (Open Anyway). Two more things the first
time a new version runs:

- macOS asks whether Sultana may use "Sultana bridge token" in your keychain.
  Choose **Always Allow**. (Each unsigned version looks like a new app to
  the keychain.)
- If Settings says the plugin is "Installed from another version of
  Sultana", click **Update Plugin**.

## Uninstalling

1. Settings ▸ Setup ▸ Claude Code ▸ **Remove**, and turn off Open at login.
2. Quit Sultana and move it to the Bin.
3. To remove everything else it kept:
   ```bash
   rm -rf ~/Library/Logs/Sultana ~/Library/Application\ Support/Sultana
   defaults delete com.lachlansleight.sultana.dock
   security delete-generic-password -s com.lachlansleight.sultana.dock -a bridge-token
   ```
