OBS 67 COUNTER v2.0.0
======================

QUICK INSTALL
-------------
1. Close OBS Studio.
2. Run "OBS-67-Counter-Setup-v2.0.0.exe".
3. Keep the default OBS folder, or browse to your OBS folder if you use a portable/custom install.
4. Finish the installer and open OBS.

ADD THE FILTER
--------------
1. In OBS, right-click your camera/video source.
2. Click Filters.
3. Under Effect Filters, click +.
4. Choose "67 Counter".

WHAT IT DOES
------------
The plugin uses pose landmarks to track both forearms and wrists.
Each stable left-high/right-low <-> left-low/right-high switch counts as one "67".

RECOMMENDED DEFAULTS
--------------------
Pose confidence: 0.20
Vertical dead zone: 0.06
Stable samples: 2
Cooldown: 100 ms

These are already the defaults in this release.

COUNTER TEXT
------------
Create a normal OBS text source first.
Then open the 67 Counter filter and select that source under:
Counter -> Counter text source

TEMPORARY CHALLENGE
-------------------
Create another OBS text source and select it under:
Temporary Challenge -> Temporary counter text source

Choose the challenge duration in the filter.
Press "Start temporary counter now" to test it.

STREAMER.BOT
------------
The plugin exposes a global OBS hotkey named:

67Counter.StartTemporary

With an OBS Raw Request in Streamer.bot, use:

{
  "requestType": "TriggerHotkeyByName",
  "requestData": {
    "hotkeyName": "67Counter.StartTemporary"
  }
}

The plugin handles resetting the temporary counter, counting, timing, and clearing the text.

TROUBLESHOOTING
---------------
- If "67 Counter" does not appear, restart OBS after installation.
- If the installer cannot find OBS, browse to the main OBS folder containing:
  bin
  data
  obs-plugins
- If tracking misses fast movements, first try the included defaults before changing Advanced settings.
- "Show green forearm tracking" can be enabled while testing alignment.

UNINSTALL
---------
Windows Settings -> Apps -> Installed apps -> OBS 67 Counter -> Uninstall

Built for Windows x64 / OBS Studio 32.x.
