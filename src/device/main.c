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

// Sprint active bitfield for tracking buttons held.
#define FORWARD_ACTIVE 0x1
#define BACKWARD_ACTIVE 0x2
#define LEFT_ACTIVE 0x4
#define RIGHT_ACTIVE 0x8
#define FORWARD_INACTIVE (~FORWARD_ACTIVE) & 0xF
#define BACKWARD_INACTIVE (~BACKWARD_ACTIVE) & 0xF
#define LEFT_INACTIVE (~LEFT_ACTIVE) & 0xF
#define RIGHT_INACTIVE (~RIGHT_ACTIVE) & 0xF

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
    static uint64_t last_backward_press = 0;
    static uint64_t last_left_press = 0;
    static uint64_t last_right_press = 0;
    static int sprint_active = 0;

    // This seems like a good place to read key inputs.
    ATOMIC({
        if (controls_available)
        {
            controls_available = 0;
            controls_needed = 1;

            jvs_buttons_t pressed = maple_buttons_pressed();
            jvs_buttons_t released = maple_buttons_released();

            // "Enter" keypress, mapped to 1P start.
            if (pressed.player1.start)
            {
                I_SendInput(ev_keydown, KEY_ENTER);
            }
            if (released.player1.start)
            {
                I_SendInput(ev_keyup, KEY_ENTER);
            }

            // "Escape" keypress, mapped to 2P start.
            if (pressed.player2.start)
            {
                I_SendInput(ev_keydown, KEY_ESCAPE);
            }
            if (released.player2.start)
            {
                I_SendInput(ev_keyup, KEY_ESCAPE);
            }

            // Normal movement, strafing when strafe modifier held, sprinting when double-tapped.
            if (pressed.player1.left)
            {
                if (sprint_active == 0)
                {
                    uint64_t cur_left_press = _get_time();
                    if (cur_left_press > 0)
                    {
                        // See if we can work out when the last tap was, for double-tapping
                        // to sprint.
                        if (last_left_press == 0)
                        {
                            // Our last tap was a sprint, or we have never gone left before.
                            last_left_press = cur_left_press;
                        }
                        else
                        {
                            // If the last tap was the right amount of time, then simulate a
                            // sprint key press.
                            uint64_t us_apart = cur_left_press - last_left_press;
                            if (us_apart > 1000 && us_apart <= MAX_DOUBLE_TAP_SPRINT)
                            {
                                sprint_active |= LEFT_ACTIVE;
                                I_SendInput(ev_keydown, KEY_RSHIFT);
                            }
                            else
                            {
                                // Just overwrite the last tap.
                                last_left_press = cur_left_press;
                            }
                        }
                    }
                }
                else
                {
                    // Gotta keep track of the fact that we're still holding movement buttons.
                    sprint_active |= LEFT_ACTIVE;
                }
                I_SendInput(ev_keydown, KEY_LEFTARROW);
            }
            if (released.player1.left)
            {
                // If we were sprinting we need to undo that.
                int old_active = sprint_active;
                sprint_active &= LEFT_INACTIVE;
                if (old_active && !sprint_active)
                {
                    I_SendInput(ev_keyup, KEY_RSHIFT);
                    last_forward_press = 0;
                    last_backward_press = 0;
                    last_left_press = 0;
                    last_right_press = 0;
                }
                I_SendInput(ev_keyup, KEY_LEFTARROW);
            }

            if (pressed.player1.right)
            {
                if (sprint_active == 0)
                {
                    uint64_t cur_right_press = _get_time();
                    if (cur_right_press > 0)
                    {
                        // See if we can work out when the last tap was, for double-tapping
                        // to sprint.
                        if (last_right_press == 0)
                        {
                            // Our last tap was a sprint, or we have never gone right before.
                            last_right_press = cur_right_press;
                        }
                        else
                        {
                            // If the last tap was the right amount of time, then simulate a
                            // sprint key press.
                            uint64_t us_apart = cur_right_press - last_right_press;
                            if (us_apart > 1000 && us_apart <= MAX_DOUBLE_TAP_SPRINT)
                            {
                                sprint_active |= RIGHT_ACTIVE;
                                I_SendInput(ev_keydown, KEY_RSHIFT);
                            }
                            else
                            {
                                // Just overwrite the last tap.
                                last_right_press = cur_right_press;
                            }
                        }
                    }
                }
                else
                {
                    // Gotta keep track of the fact that we're still holding movement buttons.
                    sprint_active |= RIGHT_ACTIVE;
                }
                I_SendInput(ev_keydown, KEY_RIGHTARROW);
            }
            if (released.player1.right)
            {
                // If we were sprinting we need to undo that.
                int old_active = sprint_active;
                sprint_active &= RIGHT_INACTIVE;
                if (old_active && !sprint_active)
                {
                    I_SendInput(ev_keyup, KEY_RSHIFT);
                    last_forward_press = 0;
                    last_backward_press = 0;
                    last_left_press = 0;
                    last_right_press = 0;
                }
                I_SendInput(ev_keyup, KEY_RIGHTARROW);
            }

            if (pressed.player1.up)
            {
                if (sprint_active == 0)
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
                                sprint_active |= FORWARD_ACTIVE;
                                I_SendInput(ev_keydown, KEY_RSHIFT);
                            }
                            else
                            {
                                // Just overwrite the last tap.
                                last_forward_press = cur_forward_press;
                            }
                        }
                    }
                }
                else
                {
                    // Gotta keep track of the fact that we're still holding movement buttons.
                    sprint_active |= FORWARD_ACTIVE;
                }
                I_SendInput(ev_keydown, KEY_UPARROW);
            }
            if (released.player1.up)
            {
                // If we were sprinting we need to undo that.
                int old_active = sprint_active;
                sprint_active &= FORWARD_INACTIVE;
                if (old_active && !sprint_active)
                {
                    I_SendInput(ev_keyup, KEY_RSHIFT);
                    last_forward_press = 0;
                    last_backward_press = 0;
                    last_left_press = 0;
                    last_right_press = 0;
                }
                I_SendInput(ev_keyup, KEY_UPARROW);
            }

            if (pressed.player1.down)
            {
                if (sprint_active == 0)
                {
                    uint64_t cur_backward_press = _get_time();
                    if (cur_backward_press > 0)
                    {
                        // See if we can work out when the last tap was, for double-tapping
                        // to sprint.
                        if (last_backward_press == 0)
                        {
                            // Our last tap was a sprint, or we have never gone backward before.
                            last_backward_press = cur_backward_press;
                        }
                        else
                        {
                            // If the last tap was the right amount of time, then simulate a
                            // sprint key press.
                            uint64_t us_apart = cur_backward_press - last_backward_press;
                            if (us_apart > 1000 && us_apart <= MAX_DOUBLE_TAP_SPRINT)
                            {
                                sprint_active |= BACKWARD_ACTIVE;
                                I_SendInput(ev_keydown, KEY_RSHIFT);
                            }
                            else
                            {
                                // Just overwrite the last tap.
                                last_backward_press = cur_backward_press;
                            }
                        }
                    }
                }
                else
                {
                    // Gotta keep track of the fact that we're still holding movement buttons.
                    sprint_active |= BACKWARD_ACTIVE;
                }
                I_SendInput(ev_keydown, KEY_DOWNARROW);
            }
            if (released.player1.down)
            {
                // If we were sprinting we need to undo that.
                int old_active = sprint_active;
                sprint_active &= BACKWARD_INACTIVE;
                if (old_active && !sprint_active)
                {
                    I_SendInput(ev_keyup, KEY_RSHIFT);
                    last_forward_press = 0;
                    last_backward_press = 0;
                    last_left_press = 0;
                    last_right_press = 0;
                }
                I_SendInput(ev_keyup, KEY_DOWNARROW);
            }

            // Fire, mapped to 1P button 1.
            if (pressed.player1.button1)
            {
                I_SendInput(ev_keydown, KEY_RCTRL);
            }
            if (released.player1.button1)
            {
                I_SendInput(ev_keyup, KEY_RCTRL);
            }

            // Use, mapped to 1P button 2.
            if (pressed.player1.button2)
            {
                I_SendInput(ev_keydown, ' ');
            }
            if (released.player1.button2)
            {
                I_SendInput(ev_keyup, ' ');
            }

            // Strafe modifier, mapped to 1P button 3.
            if (pressed.player1.button3)
            {
                I_SendInput(ev_keydown, KEY_RALT);
            }
            if (released.player1.button3)
            {
                I_SendInput(ev_keyup, KEY_RALT);
            }

            // Automap modifier, mapped to 1P button 6.
            if (pressed.player1.button6)
            {
                I_SendInput(ev_keydown, KEY_TAB);
                I_SendInput(ev_keyup, KEY_TAB);
            }
            if (released.player1.button6)
            {
                I_SendInput(ev_keydown, KEY_TAB);
                I_SendInput(ev_keyup, KEY_TAB);
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
