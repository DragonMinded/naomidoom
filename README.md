# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory. Make sure the WAD file is all lowercase or it will not be found!

## Current Issues

* The inputs are currently not configurable at all and are missing some important mappings (can't pull up automap, can't change weapon, can't sprint).
* There are currently no sound effects.
* After the demo attract loop the game will crash, rebooting the Naomi.
* Selecting "Read This" will crash, rebooting the Naomi.
* There is no way to exit back out of the config menu once entered.
* WAD file searching to determine game mode should be case-insensitive.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
