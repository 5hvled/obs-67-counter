# OBS 67 Counter

Experimental OBS Studio 32.x filter for counting the "67" alternating-hands gesture.

## V1
The filter watches frames from the OBS source it is attached to. A lightweight motion/skin-region tracker estimates two hand centroids and counts a transition whenever their vertical ordering flips and remains stable.

This first build deliberately includes a debug overlay and tuning controls so detection can be calibrated to Khaled's camera/lighting before a heavier hand-landmark model is embedded.

## Install (Windows x64)
Build the Release configuration, then copy `obs-67-counter.dll` into `obs-studio/obs-plugins/64bit/`.

In OBS, right-click the camera source -> Filters -> Effect Filters -> **67 Counter**.

## Defaults
- Detection enabled
- Dead zone: 8% of frame height
- Stable frames: 4
- Cooldown: 250 ms
- Minimum blob area: 0.35% of frame
- Debug overlay enabled

## Counter
The current count is shown by the filter's debug overlay. V2 will expose a dedicated transparent counter source and hotkeys after camera calibration.
