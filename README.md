# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. If you want to compile it, you will need to
first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will also need
to add a WAD file to the `romfs/` directory. I recommend buying Doom Ultimate from GoG and taking DOOM.WAD
from the installed files.

All features of the original Doom should be present including game loading/saving, options menus, directional
sound, WAD auto-discovery and other expected features. Note that the Naomi only has 32KB of battery-backed
SRAM to work with, so only a small number of game saves will fit. There is a fully-featured game settings
menu in the Naomi Test Mode including control remapping, various options and the ability to reset defaults
and wipe game saves.

Note that this is neither designed for, nor will ever be coded to take coin drops. This is a hobby port and
I don't want people making money running this in public based on my hard work. Somebody else may do the work
to make this happen, but I certainly won't be that person!

## Releases

A precompiled release with the Doom 1 Shareware WAD is available under the `shareware/` directory. Load
it with Demul or net boot it on your Naomi cabinet to play it! As I don't have the rights to distribute
commercial versions of Doom, there is no compiled version of Doom Ultimate or other versions. If you have
the WADs, compile it yourself! Alternatively, you can visit https://doom.dragonminded.com/ which hosts a
version of the `wadinjector/` app and provide a WAD that you own to get a compiled version of Doom for Naomi
that includes that WAD.

## Working WAD Files

The following WADs have been tested working with Doom for Sega Naomi:

* Original Doom shareware WAD (DOOM1.WAD).
* Doom Ultimate WAD (DOOM.WAD or DOOMU.WAD, depending on source).
* Doom II WAD (DOOM2.WAD).

## Default Controls

* The 1P joystick controls forward/reverse and turning left/right.
* Double-tapping any direction will put you in sprint mode until you stop moving again.
* 1P Start is used to, well, start the game. It also is used for selecting menu entries.
* 2P Start is used to back out of menus and to pull up the options menu in-game.
* 1P Button 1 is fire.
* 1P Button 2 is use.
* 1P Button 3 is the strafe modifier. Holding this down causes the joystick left/right to act as strafe instead of turn.
* 1P Button 4 is switch to previous weapon.
* 1P Button 5 is switch to next weapon.
* 1P button 6 is the automap modifier. Holding this down causes the automap to display.

Note that all six buttons are remappable in the game test menu if you wish to rearrange controls. You will
also find settings there to disable double-tap sprinting, enable holding the use key down to enable sprinting
and using 2P buttons 1-6 to fast-switch weapons.

## Known Issues

Sometimes saving a game will fail, due to the SRAM being completely out of space. Try saving over another
slot to reuse that slot's space. Given the way Doom was coded, there needs to be a back button (which is mapped
to 2P start) in order to back out of menus and cancel certain operations. Doom will not work very well on a
1P-only panel as a result. It will also not work very well on 3 button panels since it needs to many controls
to be playable.

## Acknowledgements

Check out the original port here: https://github.com/stoffera/fbdoom
Thanks to https://twitter.com/_GeekMan_ for alpha testing and sound issue bug reports
