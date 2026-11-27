# MOVA Panel

Native MOVAhome controller for the BAUHN/ALDI display. Select rooms and start,
pause or stop cleaning directly from the display, without Home Assistant or a
PC relay. Uses the unofficial protocol from
[dreame-vacuum](https://github.com/Tasshack/dreame-vacuum).

## Installation

You need a MOVAhome account, WPA2 Wi-Fi and this repository's SD autostart
firmware hook. If Hello World or Home Panel already runs from your card, no
additional firmware flash is needed. ADB must be available on your computer.

1. **Prepare the SD card on your computer.** Copy
   [`sd-card/mova-panel/`](../sd-card/mova-panel/) to the root of your FAT32 data
   card. Copy this ready-to-run folder, not the source folder containing this
   README. Existing `hello-world/` and `home-panel/` folders can stay.

2. **Enter your account and Wi-Fi settings.** In the card's `mova-panel/` folder,
   copy `secrets.example.json` to `secrets.json`. Set:
   - `demo`: `0` for your real robot (`1` shows an offline demo).
   - `username` and `password`: your MOVAhome login.
   - `region`: `eu`, `us` or `sg`, matching your account.
   - `wifi_ssid` and `wifi_password`: your WPA2 Wi-Fi credentials.
   - Leave `device_id` and `expected_model` empty for now; you will discover them below.

3. **Choose the orientation, if needed.** Edit `start.sh` on the card:

   ```sh
   orientation=portrait
   # or: orientation=landscape
   ```

   Portrait is the default. Turn the display counterclockwise so its original
   right edge is at the top. Orientation cannot be changed on screen.

4. **Insert the card and find your robot.** Boot the display with the card
   inserted and connect it to your computer by USB. In a terminal **on your
   computer**, open an ADB shell:

   ```sh
   adb shell
   ```

   On Windows, use the path to your downloaded ADB, for example
   `.\platform-tools\adb.exe shell`.

   Then run these commands **inside the ADB shell on the display**:

   ```sh
   cd /mnt/extsd/mova-panel
   ./mova-panel --prepare-network secrets.json
   /bin/sh network-start.sh
   ./mova-panel --discover secrets.json
   ```

   Discovery only reads your account's device list; it does not start cleaning.
   Note the returned `device_id` and `expected_model` for your robot, then type
   `exit` to leave the ADB shell.

5. **Complete the settings and enable autostart.** Power off the display before
   removing the card. Open the card on your computer and enter the discovered
   ID and model in `mova-panel/secrets.json`, keeping both as quoted strings.
   Back up the card's root `autostart.sh`, if present, then replace it with
   [this MOVA autostart script](autostart.sh).

6. **Start MOVA Panel.** Reinsert the card and boot the display. It connects to
   Wi-Fi and loads your rooms automatically. Choose rooms and tap **Start**.
   Only one display application should run at a time.

**Keep `secrets.json` private.** Do not commit or share it. To switch back to
Hello World or Home Panel, restore that application's root autostart script.
The public repository's default `sd-card/autostart.sh` still selects Hello World.

## Use

- **All rooms** selects the entire current map; tap room buttons for individual rooms.
- **Refresh** reloads room names and IDs. There is no hourly automatic refresh.
- **Clear selection** deselects everything. **Next** appears only with more than eight rooms.
- **Start** / **Start cleaning**, **Pause** and **Stop** control the robot.

Cleaning uses the last manual settings saved in MOVAhome. CleanGenius is disabled
before starting; the saved vacuum/mop mode stays unchanged. Suction and water
values are read from the robot. A changed map cancels the start and refreshes
the room list so you can select again. Check MOVAhome before repeating a command
whose result is uncertain.

## Development

The layout matches Home Panel: sources and build tools live in `mova-panel/`;
only runtime files and license notices live in `sd-card/mova-panel/`.

From the source `mova-panel/` folder, with Python 3.10+:

```sh
python -m pip install -r requirements.txt
python build.py
```

The output is `build/sd-card/`. The first build downloads Mbed TLS and zlib using
the versions and SHA-256 checksums in `dependencies.lock.json`. Downloads stay
in ignored `build/dependencies/`; no separate source archive is generated.
The display itself needs no library downloads. Stop the running application
before replacing its executable or removing the card.

Optional tests:

```sh
python -m unittest discover -s . -p test_build_deps.py
python test_device.py
```

The second command needs an ADB-connected display and `adb` on PATH. Tests do
not start robot cleaning. For read-only diagnostics, run `--probe`, `--rooms`,
`--inspect` or `--plan` with `secrets.json` in the display's ADB shell.

## Compatibility and licenses

Tested with MOVAhome and a V70 Ultra Complete (`mova.vacuum.r59795`): login,
room loading, Wi-Fi, portrait/landscape and injected touch input. Actual cleaning
execution, physical finger input and a full cold boot remain unverified.
Other models and encrypted/external map formats may not work. No map switching
or live cleaning-progress display. Fixed UI text is English; room names are
preserved or derived from room types (currently German).

HTTPS certificate checks remain enabled. The launcher synchronizes the clock
over SNTP (UDP 123), since the tested display starts with an incorrect clock.

Project code: GPL-2.0-or-later. Dependency/font notices are included. See
[THIRD_PARTY.md](../THIRD_PARTY.md), [vendor/README.md](vendor/README.md) and
[PROTOCOL-SOURCE.txt](PROTOCOL-SOURCE.txt).
