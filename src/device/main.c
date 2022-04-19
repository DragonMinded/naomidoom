#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <naomi/romfs.h>
#include <naomi/maple.h>
#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"
#include "../d_event.h"

int main()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // Init our filesystem.
    romfs_init_default();

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
