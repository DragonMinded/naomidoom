# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory.

## Releases

A precompiled release with the Doom 1 Shareware WAD is available under the `shareware/` directory. Load
it with Demul or net boot it on your Naomi cabinet to play it!

## Working WAD Files

The following WADs have been tested working with Doom for Sega Naomi:

* Original Doom shareware WAD (DOOM1.WAD).
* Doom Ultimate WAD (DOOM.WAD or DOOMU.WAD, depending on source).

## Controls

* The 1P joystick controls forward/reverse and turning left/right.
* Double-tapping any direction will put you in sprint mode until you stop moving again.
* 1P Start is used to, well, start the game. It also is used for selecting menu entries.
* 2P Start is used to back out of menus.
* 1P Button 1 is fire.
* 1P Button 2 is use.
* 1P Button 3 is the strafe modifier. Holding this down causes the joystick left/right to act as strafe instead of turn.
* 1P Button 4 is switch to previous weapon.
* 1P Button 5 is switch to next weapon.
* 1P button 6 is the automap modifier. Holding this down causes the automap to display.

## Current Issues

* The inputs are currently not configurable at all.
* SFX are not directional nor do they update when you move around.
* Load/Save game are completely untested.
* Settings don't load/save from EEPROM like they should.
* There are no settings in test mode, where game settings should live.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
