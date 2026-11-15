# Resolution profiles

Resolution profiles are optional and are disabled by default. In a game profile's or Monitor Customization's **Resolution** tab, choose a supported display mode and scaling mode. The monitor profile's resolution is the fallback for the desktop and games without a resolution override. Lumetra offers the modes enumerated by Windows for every attached display, so a mode that only exists on a second monitor can still be picked. Stretch and no-scaling choices are unavailable when the display driver does not accept a fixed-output scaling for that mode.

The **Settings → Display** group controls when saved profiles are used:

- **Auto-Switch** applies the game's saved resolution profile when it launches.
- **Ask Before Applying** displays a Lumetra popup with the current and proposed modes and Apply/Dismiss actions.
- **Off** disables resolution switching. This is the default.

When resolution switching is enabled, the monitor profile's selected mode is applied as the desktop/unmanaged-app fallback. Per-game profiles override it; games without their own mode use the monitor fallback. Monitor fallback changes are applied without a prompt, while **Ask Before Applying** prompts for game-specific overrides.

## Which display is changed

Every mode change targets one concrete display device (`\\.\DISPLAY1`, `\\.\DISPLAY2`, …), not "the default display":

- The **monitor profile** is applied to Windows' primary display, the one flagged as the main display in Display Settings.
- A **game profile** is applied to the display the game window is on. A game running on a second monitor therefore never resizes the display you are working on.

If the targeted display does not offer the requested mode, Lumetra falls back to another attached display that does.

## Keeping the desktop where it is

`DM_POSITION` in a `DEVMODE` is a *reposition* request, not a resolution hint. Writing it while switching resolution is what makes a screen end up in the top-left corner of the virtual desktop, so Lumetra never sets it for a mode change and lets Windows keep the placement of the display.

Some drivers re-flow the whole desktop layout when the mode of one display changes, which moves the other displays as well. After every change Lumetra compares the placement of all attached displays with the placement Windows keeps in its registry and moves anything that was displaced back to where the saved layout says it belongs.

The mode that is restored is the one Windows keeps in its registry (`ENUM_REGISTRY_SETTINGS`), which is the user's saved desktop configuration; a fullscreen game only ever replaces the live mode. Lumetra also keeps the mode the display was running before its first change, and uses it if Windows rejects the saved one.

## Switching

Resolution selection is independent of Lumetra's color/gamma profiles. Switching follows foreground-game transitions. If a game loses foreground or its tracked window closes, Lumetra restores the mode captured before its first temporary change. The original mode is also restored when switching is paused or Lumetra shuts down cleanly. When multiple supported games are open, the foreground game's profile takes precedence.

Windows exposes default, stretch, and center fixed-output behavior rather than a universal maintain-aspect-ratio setting. Lumetra maps **Maintain aspect ratio** to the display driver's default fixed-output behavior; the exact result therefore depends on the active driver/display configuration. Stretch and no-scaling are only selectable when the display driver accepts the fixed-output scaling for that mode, which Lumetra probes with `CDS_TEST` instead of guessing from the enumerated modes.

Mode changes use complete modes enumerated by Windows for the targeted display and are checked with `CDS_TEST` before they are applied. The change is temporary: Lumetra does not write a new global display default. Lumetra verifies the resulting dimensions and the placement of every display, and rolls an unexpected result back to the mode that was running before.

A prompt is offered once per game launch. Lumetra recognises a game launch by its process id *and* its start time, so re-launching a game prompts again even when Windows hands the new process the id of the old one. For anti-cheat protected games that do not allow their start time to be read, the window that was foreground when the prompt was shown is used instead. Clicking **Apply** no longer requires the game to still be the foreground window, which is what silently broke the popup after a game had been restarted.

The resolution popup can optionally play `Sounds/PopupSfx.wav` or `Sounds/PopupSfx.mp3`. Sound playback is skipped safely when neither file is installed.

Resolution changes can interrupt a display or move windows. The Settings → Display page shows a warning while Auto-Switch is selected.
