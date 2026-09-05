# TRENCHRUNNER

Atari's 1983 *Star Wars* arcade game running on the Waveshare ESP32-C6-LCD-1.69
medal (the PELLETINO hardware). Not a port: an emulator of the original board,
driven by the original ROMs.

## What the original hardware is

Two Motorola 6809E CPUs at 1.512 MHz. The main CPU drives Atari's Analog Vector
Generator (AVG), a display-list sequencer stepped by a 256-byte state PROM, and
a custom matrix processor ("mathbox") built from AMD 2901 bit slices and four
microcode PROMs. The sound CPU has a 6532 RIOT, four POKEY chips and a TMS5220
speech synthesizer. The monitor is a horizontally mounted color vector display.

## Status

- Main board: boots, plays, takes yoke input. Mathbox, divider, PRNG, ROM
  banking and the AVG are ports of MAME's implementations (BSD-3-Clause).
- Sound board: 6809, RIOT timer/PA7 interrupts, POKEY music and effects.
  TMS5220 speech accepts data and reports status; synthesis is not done yet.
- ESP32: full game speed at the game's own vector frame rate (~27-40 fps),
  rendering in a separate task, tilt yoke, BOOT button fires, PWR short press
  inserts a coin and re-centers the yoke, PWR long press powers off.

## Layout

```
core/    platform-independent emulation (C): e6809.c, avg.c, starwars.c,
         sound.c, pokey.c, tms5220.c
host/    harness that boots the ROMs on a PC, dumps frames as PPM/PNG and
         audio as WAV, and cross-checks the AVG fast path (make; ./harness)
main/    ESP-IDF application: main loop, renderer task, input, ROM headers
components/  display (ST7789), imu (QMI8658), audio_hal (ES8311/I2S), emu
tools/   convert_roms.py: MAME 'starwars' set -> main/roms/starwars_roms.h
```

## Building

ROMs are not included. Put the MAME `starwars` set in `starwars/` and run
`python3 tools/convert_roms.py starwars`. Then:

```
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker build
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem* -b 460800 write_flash @build_docker/flash_args
```

Host harness: `cd host && make && ./harness ../starwars out 20 --dsw1 0 --script "3:fire=1,3.3:fire=0"`.

## Licenses

- `core/e6809.c`: from vecx (GPL-3.0).
- AVG, mathbox, divider logic and the TMS5220 tables: ported from MAME (BSD-3-Clause).
- Everything else: MIT.
