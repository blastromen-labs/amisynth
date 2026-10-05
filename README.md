# Amiga Synth

A software synthesizer for the Amiga 1200. It takes over the machine: the sequencer, the synth controls, and a drawable oscillator.

![Synth controls](docs/synth.png)

![Sequencer](docs/sequencer.png)

![Drawable oscillator](docs/draw.png)

## Run

Copy `amisynth.exe` to the Amiga. From the Shell, run it. **EXIT** returns to Workbench. Pressing both mouse buttons does the same.

A stock PAL A1200 and a mouse are enough. Copy `amisynth.exe.info` next to the program if you want the icon in a Workbench drawer. The icon file has to keep the same name as the program.

## Releases

Publishing a GitHub release builds `amisynth.exe` and attaches it to that release, together with the Workbench icon. The same workflow can be run by hand from the Actions tab, and that run attaches the files to the latest release.

## Build

Build with the [Amiga Debug](https://github.com/BartmanAbyss/vscode-amiga-debug) toolchain (`m68k-amiga-elf-gcc` and `elf2hunk`) on your `PATH`:

```sh
make
```

The program is written to `out/amisynth.exe`.
