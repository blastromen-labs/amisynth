# Amiga Synth

A software synthesizer for the Amiga 1200: the sequencer, the synth controls, and a drawable oscillator. It runs in a Workbench window, on a screen of its own, or with the whole machine to itself.

![Synth controls](docs/synth.png)

![Sequencer](docs/sequencer.png)

![Drawable oscillator](docs/draw.png)

## Run

Copy `amisynth.exe` to the Amiga and run it from the Shell, or double-click its icon. **EXIT**, the window's close gadget, Esc, or both mouse buttons quit.

A stock PAL A1200 with Kickstart 3.0 or newer and a mouse are enough. Copy `amisynth.exe.info` next to the program if you want the icon in a Workbench drawer. The icon file has to keep the same name as the program.

There are three ways to run it:

| Mode | Switch | What happens |
| --- | --- | --- |
| Window | `WINDOW` (the default) | Opens a window on Workbench. AmigaOS keeps running, and the sequencer clock, audio channels and mouse come from the OS |
| Screen | `SCREEN` | Opens a lowres screen of its own, still under AmigaOS. The window falls back to this when Workbench is too small for it |
| Takeover | `TAKEOVER` | Turns AmigaOS off and drives the hardware directly, as earlier versions did |

On a hires Workbench such as PAL:High Res the window doubles every pixel across so the picture keeps its shape. When the window borders leave a few columns too few, up to `WINDOW_MAX_DROP` doubled columns are left out, spread evenly over the picture. If another program holds the audio channels, the window runs silent and says so in the Shell.

From Workbench, the same switches are tooltypes in the icon's Information window. The icon comes with them in brackets, which means off; delete the brackets to turn one on. For example, to start in takeover mode:

1. Click the **amisynth.exe** icon once to select it.
2. Choose **Icons → Information...** (Right Amiga + I).
3. In the **Tool Types** list, click `(TAKEOVER)`. It appears in the text field below the list.
4. Change it to `TAKEOVER`, press **Return** and click **Save**.
5. Double-click the icon.

Put the brackets back to return to window mode. If the Tool Types list is empty, the icon is from an older build: click **New**, type `TAKEOVER`, press **Return** and click **Save**, or copy a new `amisynth.exe.info` over it.

To try a mode once without changing the icon, choose **Workbench → Execute Command...** and give the program's full path with the switch, such as `Work:amisynth/amisynth.exe TAKEOVER`.

## FX

The **FX** tab has distortion (**DRIVE**), a tempo delay (**STEP** in sequencer sixteenths, **LEVEL**, **FDBK**) and a reverb (**DECAY**, **LEVEL**). Paula loops single-cycle waves, so delay and reverb replay the notes themselves on the right-hand voices while the dry sound stays on the left. With both levels at zero, both sides play the dry sound as before. Drive clips the wave shape and the envelope: saw, triangle and filtered sounds get grittier, and every note holds up longer as it fades. A plain square or pulse is already fully clipped, so on those you hear the sustain rather than a change in tone. The ranges and defaults are in `src/config.h`.

## Troubleshooting on a real Amiga

The program writes no log unless asked. `LOG=amisynth.log` (or the `LOG` tooltype) writes one, falling back to `RAM:amisynth.log` when that drawer is write-protected; `DIAG_LOG_DEFAULT` in `src/config.h` turns it on for every run. Lines written before the program takes over the machine are on disk at once, so the log shows how far startup got even if the machine has to be reset. The log lists the Kickstart, CPU, chipset, Workbench screen mode and monitor, free memory and every startup stage.

In window and screen mode AmigaOS stays in charge, so every log line is on disk at once. A crash in the program's own code (an address error, `8000 0003` on a Software Failure, and the like) is caught through exec's task trap instead: the program closes its window, writes the same crash record as in takeover mode and says so in the Shell. Faults inside AmigaOS interrupts still bring up the usual requester. If takeover mode shows a black screen, try the window first: if that works, the problem is in the hardware takeover.

In takeover mode the program keeps its log in memory and writes it out when it exits. If the program crashes (bus error, illegal instruction and the like) or its main loop stops drawing for a few seconds, it shows a red or orange screen, gives the machine back to AmigaOS and writes a crash record: the stage, the program counter, the registers and the stack.

In takeover mode, until the picture appears, the background colour shows how far startup got:

| Colour | Last step reached |
| --- | --- |
| Black | Display and interrupts are off |
| Purple | The machine is taken over |
| Teal | Crash catching is armed |
| Red | A CPU exception was caught |
| Orange | The watchdog caught a stalled main loop |

Switches (or tooltypes) narrow a problem down:

| Switch | Effect |
| --- | --- |
| `LOG=<file>` / `NOLOG` | Write a log to that file, or none even when logging is on by default |
| `SERIAL` | Also send every log line to the serial port (9600 baud), live even while the program owns the machine |
| `NOTICKS` | Do not start the sequencer clock interrupt |
| `NOAUDIO` | Leave Paula alone |
| `NOCATCH` | Do not catch crashes: no task trap, and in takeover no CPU exception vector hooks |
| `WATCHDOG=<s>` | Takeover only: seconds without a frame before the watchdog acts, 0 turns it off |
| `SETTLE=<ticks>` | Takeover only: wait this long (1/50 s) for disk activity to finish first |
| `CRASHTEST` / `HANGTEST` | Crash or hang on purpose after two seconds, to check that logging works on that machine |
| `QUITAFTER=<frames>` | Quit by itself after that many frames. In window and screen mode it first reads the window back and logs how many pixels differ from the picture |

The log also says which clock drives the sequencer (a CIA-B timer, or the vertical blank when both are taken), how big the window picture came out, and how many frames were drawn. The clock ticks 4 times per video frame, so 4 × frames ÷ ticks is the share of video frames that got a new picture.

To turn a crash record into source lines, use the `amisynth.elf` from the same build (the build id is on the first log line; releases attach it):

```sh
python3 tools/crashdecode.py amisynth.log out/amisynth.elf
```

The defaults are in `src/config.h`.

`make OPT=-O0` builds without optimisation, to rule the optimiser out (remove `obj/*.o` first so everything is recompiled). That build runs at about one frame a second and its clock interrupt nearly overruns, so it is only for testing.

## Releases

Publishing a GitHub release builds `amisynth.exe` and attaches it to that release, together with the Workbench icon and `amisynth.elf` for decoding crash logs. The same workflow can be run by hand from the Actions tab, and that run attaches the files to the latest release.

## Build

Build with the [Amiga Debug](https://github.com/BartmanAbyss/vscode-amiga-debug) toolchain (`m68k-amiga-elf-gcc` and `elf2hunk`) on your `PATH`:

```sh
make
```

The program is written to `out/amisynth.exe`.
