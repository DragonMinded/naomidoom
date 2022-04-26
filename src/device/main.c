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
#include <naomi/interrupt.h>
#include <naomi/posix.h>
#include <sys/time.h>
#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"
#include "../d_event.h"

int controls_available = 0;
int controls_needed = 0;

#define STDERR_LEN 8192
char stderr_buf[STDERR_LEN + 1];

int _stderr_write(const char * const data, unsigned int len)
{
    uint32_t old_irq = irq_disable();
    int location = strlen(stderr_buf);
    int available = STDERR_LEN - location;

    int amount = available >= len ? len : available;
    memcpy(stderr_buf + location, data, amount);
    stderr_buf[location + amount] = 0;

    irq_restore(old_irq);
    return amount;
}

stdio_t error_hook = { 0, 0, _stderr_write };

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

void I_DrawErrorScreen()
{
    // I know this isn't the best, since it locks out GDB, but it is what it is.
    uint32_t old_irq = irq_disable();

    video_init(VIDEO_COLOR_1555);
    video_set_background_color(rgb(48, 48, 48));
    video_draw_debug_text(16, 16, rgb(255, 255, 255), stderr_buf);
    video_display_on_vblank();

    while ( 1 ) { ; }

    // We should never get here.
    irq_restore(old_irq);
}

int main()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // Set up error handling.
    memset(stderr_buf, 0, STDERR_LEN + 1);
    hook_stdio_calls( &error_hook );

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
    if (romfs_init_default() != 0)
    {
        fprintf(stderr, "Failed to init filesystem!");
        fflush(stderr);
        I_DrawErrorScreen();
    }

    // Init audio subsystem.
    audio_init();

    // Off we go!
    D_DoomMain();

    return 0;
}

// Max number of microseconds between forward taps to consider a sprint.
#define MAX_DOUBLE_TAP_SPRINT 250000

void D_PostEvent(event_t* ev);

void I_SendInput(evtype_t type, int data)
{
    event_t event;
    event.type = type;
    event.data1 = data;
    D_PostEvent(&event);
}

uint64_t _get_time()
{
    struct timeval time;
    if (gettimeofday(&time, NULL) == 0)
    {
        return (((uint64_t)time.tv_sec) * 1000000) + time.tv_usec;
    }

    return 0;
}

void I_StartTic (void)
{
    static uint64_t last_forward_press = 0;
    static int sprint_pressed = 0;

    // This seems like a good place to read key inputs.
    ATOMIC({
        if (controls_available)
        {
            controls_available = 0;
            controls_needed = 1;

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

            if (pressed.player2.start)
            {
                I_SendInput(ev_keydown, KEY_ESCAPE);
            }
            if (released.player2.start)
            {
                I_SendInput(ev_keyup, KEY_ESCAPE);
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
                uint64_t cur_forward_press = _get_time();
                if (cur_forward_press > 0)
                {
                    // See if we can work out when the last tap was, for double-tapping
                    // to sprint.
                    if (last_forward_press == 0)
                    {
                        // Our last tap was a sprint, or we have never gone forward before.
                        last_forward_press = cur_forward_press;
                    }
                    else
                    {
                        // If the last tap was the right amount of time, then simulate a
                        // sprint key press.
                        uint64_t us_apart = cur_forward_press - last_forward_press;
                        if (us_apart > 1000 && us_apart <= MAX_DOUBLE_TAP_SPRINT)
                        {
                            sprint_pressed = 1;
                            I_SendInput(ev_keydown, KEY_RSHIFT);
                        }
                        else
                        {
                            // Just overwrite the last tap.
                            last_forward_press = cur_forward_press;
                        }
                    }
                }
                I_SendInput(ev_keydown, KEY_UPARROW);
            }
            if (released.player1.up)
            {
                // If we were sprinting we need to undo that.
                if (sprint_pressed)
                {
                    I_SendInput(ev_keyup, KEY_RSHIFT);
                    sprint_pressed = 0;
                    last_forward_press = 0;
                }
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
    });
}

int mkdir(const char *_path, mode_t __mode)
{
    // Empty, we are read-only.
    return EINVAL;
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
