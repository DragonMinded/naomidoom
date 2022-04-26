# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory.

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
* Cannot choose "y" to exit game, not that there is anything to exit to.
* Sometimes there is garbage on the screen between the loading screen and the game splash screen.
* Settings don't load/save from EEPROM like they should.
* There is no test mode binary, so going into Naomi test mode just boots the game.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
