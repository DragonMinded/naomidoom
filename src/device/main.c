#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <naomi/romfs.h>
#include <naomi/maple.h>
#include <naomi/audio.h>
#include <naomi/video.h>
#include <naomi/console.h>
#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"
#include "../d_event.h"

void _draw_loading_screen()
{
    extern unsigned int loading_png_width;
    extern unsigned int loading_png_height;
    extern void *loading_png_data;

    // Display the graphic.
    video_draw_sprite(
        (video_width() - loading_png_width) / 2,
        (video_height() - loading_png_height) / 2,
        loading_png_width,
        loading_png_height,
        loading_png_data
    );

    // Flip the screens so it is visible.
    video_display_on_vblank();
}

int main()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // First, initialize a simple screen.
    video_init(VIDEO_COLOR_1555);

    // Then, enable console capture but disable console display if
    // we are not in debug mode.
    console_init(16);

#ifndef NAOMI_CONSOLE
    // Hide the debug console, we don't want to see it!
    console_set_visible(0);
#endif

    // Draw a loading screen so people know things are working.
    _draw_loading_screen();

    // Init our filesystem.
    romfs_init_default();

    // Init audio subsystem.
    audio_init();

    // Off we go!
    D_DoomMain();

    return 0;
}

void D_PostEvent(event_t* ev);

void I_SendInput(evtype_t type, int data)
{
    event_t event;
    event.type = type;
    event.data1 = data;
    D_PostEvent(&event);
}

void I_StartTic (void)
{
    // This seems like a good place to read key inputs.
    maple_poll_buttons();
    jvs_buttons_t pressed = maple_buttons_pressed();
    jvs_buttons_t released = maple_buttons_released();

    if (pressed.player1.start)
    {
        I_SendInput(ev_keydown, KEY_ENTER);
    }
    if (released.player1.start)
    {
        I_SendInput(ev_keyup, KEY_ENTER);
    }

    if (pressed.player1.left)
    {
        I_SendInput(ev_keydown, KEY_LEFTARROW);
    }
    if (released.player1.left)
    {
        I_SendInput(ev_keyup, KEY_LEFTARROW);
    }

    if (pressed.player1.right)
    {
        I_SendInput(ev_keydown, KEY_RIGHTARROW);
    }
    if (released.player1.right)
    {
        I_SendInput(ev_keyup, KEY_RIGHTARROW);
    }

    if (pressed.player1.up)
    {
        I_SendInput(ev_keydown, KEY_UPARROW);
    }
    if (released.player1.up)
    {
        I_SendInput(ev_keyup, KEY_UPARROW);
    }

    if (pressed.player1.down)
    {
        I_SendInput(ev_keydown, KEY_DOWNARROW);
    }
    if (released.player1.down)
    {
        I_SendInput(ev_keyup, KEY_DOWNARROW);
    }

    if (pressed.player1.button1)
    {
        I_SendInput(ev_keydown, KEY_RCTRL);
    }
    if (released.player1.button1)
    {
        I_SendInput(ev_keyup, KEY_RCTRL);
    }

    if (pressed.player1.button2)
    {
        I_SendInput(ev_keydown, ' ');
    }
    if (released.player1.button2)
    {
        I_SendInput(ev_keyup, ' ');
    }

    if (pressed.player1.button3)
    {
        I_SendInput(ev_keydown, KEY_RALT);
    }
    if (released.player1.button3)
    {
        I_SendInput(ev_keyup, KEY_RALT);
    }
}

void mkdir(const char *path, int perm)
{
    // Empty, we are read-only.
}

int access(const char *path, int axx)
{
    if (axx == R_OK)
    {
        FILE *fp = fopen(path, "rb");
        if (fp)
        {
            fclose(fp);
            return 0;
        }

        return EACCES;
    }

    // Empty
    return EACCES;
}
