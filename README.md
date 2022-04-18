# Doom for Sega Naomi

This is a port of the original Doom source code for the Sega Naomi platform. It is based on the work of
Kristoffer Andersen who got Doom ported to a framebuffer. Currently there is no input or sound, and after
the demo attract loop it will crash and reboot the Naomi for some reason. If you want to compile it, you
will need to first set up the libnaomi toolchain from https://github.com/DragonMinded/libnaomi and you will
also need to add a WAD file to the `romfs/` directory. Make sure the WAD file is all lowercase or it will
not be found!

Check out the original port here: https://github.com/stoffera/fbdoom
