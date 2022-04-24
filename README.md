# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory.

## Controls

Only player 1 controls are mapped on the cabinet as of right now.

* The joystick controls forward/reverse and turning left/right.
* Start is used to, well, start the game. It also is used for selecting menu entries.
* Button 1 is fire.
* Button 2 is use.
* Button 3 is the strafe modifier. Holding this down causes the joystick left/right to act as strafe instead of turn.

## Current Issues

* The inputs are currently not configurable at all and are missing some important mappings (can't pull up automap, can't change weapon, can't sprint).
* There are currently no sound effects.
* There is no way to exit back out of the config menu once entered.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
