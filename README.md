# em6502
A simple 6502 emulator - work in progress. I've made this to refresh my memory on 6502

At the moment I'm working through enough of the Op Codes so that VIC20 ROMS work.

Currently the memory map is 16K of RAM and 20K of ROM, as none of the hardware peripherals are currently emulated.

## Getting started

Run 'make' to build em6502 binary.

From http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/vic20/index.html get:

* kernal.901486-07.bin and save it as rom1.img
* basic.901486-01.bin and save it as rom2.img
* characters.901460-03.bin and save it as rom3.img

Then type ./em6502 to start emulating!

Currently the image is writtin as display.ppm every 1,000,000 clock cycles
