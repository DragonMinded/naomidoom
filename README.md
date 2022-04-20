# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory. Make sure the WAD file is all lowercase or it will not be found!

## Current Issues

* The inputs are currently not configurable at all and are missing some important mappings.
* There is currently no sound effects.
* After the demo attract loop the game will crash, rebooting the Naomi for some reason.
* Selecting "Read This" will crash, rebooting the Naomi for some reason.
* There is no way to exit back out of the config menu.
* Demo plays back far too quickly compared to the PC release.
* WAD file searching should be case-insensitive.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
