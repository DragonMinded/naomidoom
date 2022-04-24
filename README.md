# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory.

## Current Issues

* The inputs are currently not configurable at all and are missing some important mappings (can't pull up automap, can't change weapon, can't sprint).
* There are currently no sound effects.
* There is no way to exit back out of the config menu once entered.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
