# OBS 67 Counter

OBS 67 Counter is a Windows x64 OBS Studio effect filter that automatically counts the alternating "67" forearm gesture.

The current stable build uses MoveNet pose landmarks to track both shoulders, elbows, and wrists, then counts each stable left-high/right-low ↔ left-low/right-high switch.

## Friend-friendly install

Download the full release package and use:

**OBS-67-Counter-Setup-v2.0.0.exe**

The installer includes the plugin DLL, MoveNet model, and ONNX Runtime. No extra dependencies need to be installed manually.

For OBS Portable or unusual installations, choose the main OBS folder when the installer asks for it. The correct folder contains:

- `bin`
- `data`
- `obs-plugins`

A manual portable ZIP is also produced by the build.

## Add it in OBS

1. Open OBS.
2. Right-click your camera/video source.
3. Open **Filters**.
4. Under **Effect Filters**, click **+**.
5. Add **67 Counter**.

## Stable defaults

- Pose confidence: `0.20`
- Vertical dead zone: `0.06`
- Stable samples: `2`
- Cooldown: `100 ms`
- Detection enabled
- Debug logging disabled
- Green tracking overlay disabled

These values are intended to work well immediately while still exposing the Advanced controls for tuning.

## Permanent counter text

Create an OBS text source, then select it inside:

**67 Counter → Counter → Counter text source**

Prefix and postfix fields are available.

## Temporary challenge counter

Create a second OBS text source and select it inside:

**67 Counter → Temporary Challenge → Temporary counter text source**

Set the challenge duration and use **Start temporary counter now** to test it.

The temporary counter starts at 0, counts valid gestures while active, then clears automatically. The permanent total keeps counting at the same time.

## Streamer.bot

The plugin exposes a global OBS hotkey:

`67Counter.StartTemporary`

Streamer.bot can trigger it using an OBS Raw Request:

```json
{
  "requestType": "TriggerHotkeyByName",
  "requestData": {
    "hotkeyName": "67Counter.StartTemporary"
  }
}
```

The plugin handles the reset, timer, counting, and clearing internally.

## Windows / OBS compatibility

- Windows x64
- Built against OBS Studio 32.2.2
- Intended for OBS Studio 32.x

## License

See [LICENSE](LICENSE).
