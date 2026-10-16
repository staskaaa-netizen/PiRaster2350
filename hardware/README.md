# Hardware

The carrier that puts the RP2350's HSTX pairs on an LVDS cable, and the pins this
build drives.

## What is here

| file | what it is |
| --- | --- |
| `pico2lvds_schematic.pdf` | the rendered schematic of the adapter |
| `kicad_project/pico2lvds.kicad_sch` | the schematic source |
| `kicad_project/pico2lvds.kicad_pcb` | the four-layer board, 98.55 x 82.05 mm |
| `kicad_project/pico2lvds.kicad_pro` | the KiCad project |
| `kicad_project/fab/pico2lvds.zip` | the fabrication package: gerbers and drill files |
| `kicad_project/README.md` | the note that came with the design, unchanged |

## Whose design this is

This adapter is **PicoLVDS's** design, taken unchanged from
<https://github.com/ceteras/PicoLVDS>, where it lives in `kicad_project/` beside
`pico2lvds_schematic.pdf`. That project is GPL-3.0, PiRaster is GPL-3.0, and the
original note is kept with the files.

The files are here for one reason: in the project PiRaster came from, the
hardware and the firmware travel together. A carrier that is not in the
repository is a carrier nobody can check the firmware against.

## What it does

The adapter carries eight HSTX pins to the panel's LVDS input:

| pair | signal | pin |
| --- | --- | --- |
| 0 | D0+ / D0- | GP12 / GP13 |
| 1 | D1+ / D1- | GP14 / GP15 |
| 2 | D2+ / D2- | GP16 / GP17 |
| clock | CLK+ / CLK- | GP18 / GP19 |

There is **no LVDS transmitter chip**. The HSTX serialiser drives the pairs
directly, in the seven-phase packing that this project's pacer LUT produces, and
the receivers of the panel read that as LVDS. The same eight pins are stated for
the firmware in `panels/sharp_lq121s1lg44.h`, and that header is the only place
the code reads them.

The design was made for a Pico 2, and the original worked on a breadboard with a
B089AW01-V.0 and an HSD089IFW1-A00-V1.0. The LVDS cable has to be made by hand:
no commercial cable uses that layout.

## What is not written down here yet

* **This bench's carrier.** PiRaster's build targets a Waveshare RP2350-PiZero.
  Which carrier it is wired to, whether the adapter of this design was built,
  and any change made to the design all belong in this file.
* **The panel's supply and backlight**, and the connector of the Sharp
  LQ121S1LG44.
* **The build status of the board.** The note that came with the design says
  that the PCB was not tested. A board that has been built and run should say so
  here, together with the panel it was run on.
