#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <naomi/romfs.h>
#include <naomi/maple.h>
#include <naomi/audio.h>
#include <naomi/video.h>
#include <naomi/eeprom.h>
#include <naomi/console.h>
#include <naomi/interrupt.h>
#include <naomi/posix.h>
#include <naomi/thread.h>
#include <naomi/system.h>
#include <sys/time.h>
#include "../doomdef.h"
#include "../doomstat.h"
#include "../d_main.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../d_event.h"
#include "../r_defs.h"
#include "../z_zone.h"
#include "../w_wad.h"
#include "../hu_stuff.h"

int controls_available = 0;
int controls_needed = 0;

#define STDERR_LEN 8192
char stderr_buf[STDERR_LEN + 1];

int _stderr_write(const char * const data, unsigned int len)
{
    int amount;

    ATOMIC({
        int location = strlen(stderr_buf);
        int available = STDERR_LEN - location;
        amount = available >= len ? len : available;

        memcpy(stderr_buf + location, data, amount);
        stderr_buf[location + amount] = 0;
    });

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

// Defines from our other implementation files.
void _pauseAnySong();
void _disableAnyVideoUpdates();

void I_DrawErrorScreen()
{
    _pauseAnySong();
    _disableAnyVideoUpdates();
    fflush(stderr);

    video_init(VIDEO_COLOR_1555);
    video_set_background_color(rgb(48, 48, 48));
    video_draw_debug_text(16, 16, rgb(255, 255, 255), stderr_buf);
    video_display_on_vblank();

    while ( 1 ) { thread_yield(); }
}

// To share the load between the video thread (polling controls) and the main
// thread (reacting to polled controls).
mutex_t control_mutex;

int main()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // Make sure we have a mutex for control input ready.
    mutex_init(&control_mutex);

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
        I_DrawErrorScreen();
    }

    // Init audio subsystem.
    audio_init();

    // Off we go!
    D_DoomMain();

    return 0;
}

// Max number of microseconds between forward taps to consider a sprint.
#define MAX_DOUBLE_TAP_SPRINT 200000

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
void I_Error (char *error, ...) __attribute__ ((noreturn));
int P_PlayerCanSwitchWeapon (player_t* player, weapontype_t newweapon);

void I_SendInput(evtype_t type, int data)
{
    event_t event;
    event.type = type;
    event.data1 = data;
    D_PostEvent(&event);
}

#define MAX_UP_QUEUED 10
int upevents[MAX_UP_QUEUED];
int numpevents = 0;

void I_SendDelayedInput(evtype_t type, int data)
{
    if (type != ev_keyup)
    {
        I_Error("Cannot send delayed event type %d!", type);
    }

    if (numpevents == MAX_UP_QUEUED)
    {
        I_Error("Maximum queued events reached!");
    }

    upevents[numpevents] = data;
    numpevents++;
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

int _weapon_to_position(weapontype_t weapon)
{
    switch(weapon)
    {
        case wp_fist:
        case wp_chainsaw:
            return 0;
        case wp_pistol:
            return 1;
        case wp_shotgun:
        case wp_supershotgun:
            return 2;
        case wp_chaingun:
            return 3;
        case wp_missile:
            return 4;
        case wp_plasma:
            return 5;
        case wp_bfg:
            return 6;
        default:
            // Should never happen.
            I_Error("Attempt to get position of invalid weapon %d!", weapon);
    }
}

weapontype_t _position_to_weapon(int pos)
{
    switch(pos)
    {
        case 0:
            return wp_fist;
        case 1:
            return wp_pistol;
        case 2:
            return wp_shotgun;
        case 3:
            return wp_chaingun;
        case 4:
            return wp_missile;
        case 5:
            return wp_plasma;
        case 6:
            return wp_bfg;
        default:
            // Should never happen.
            I_Error("Attempt to get weapon of invalid position %d!", pos);
    }
}

void I_StartTic (void)
{
    static uint64_t last_forward_press = 0;
    static uint64_t last_backward_press = 0;
    static uint64_t last_left_press = 0;
    static uint64_t last_right_press = 0;
    static int sprint_active = 0;

    // Make sure that we don't step on the control read code in the video
    // thread, which is there so that the G2 bus doesn't get overwhelmed
    // when we try to DMA to the MIE at the same time as rendering video.
    mutex_lock(&control_mutex);

    // Send pending release events.
    for (int evt = 0; evt < numpevents; evt++)
    {
        I_SendInput(ev_keyup, upevents[evt]);
    }
    numpevents = 0;

    if (controls_available)
    {
        controls_available = 0;
        controls_needed = 1;

        jvs_buttons_t pressed = maple_buttons_pressed();
        jvs_buttons_t released = maple_buttons_released();

        if (pressed.psw1 || pressed.test)
        {
            // Request to go into system test mode.
            enter_test_mode();
        }

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

        // For weapon switching, since Doom doesn't seem to have next/previous
        // weapon buttons, we must do it ourselves!
        player_t *player = NULL;
        weapontype_t curWeapon;
        if (consoleplayer >= 0 && consoleplayer < MAXPLAYERS)
        {
            player = &players[consoleplayer];
            curWeapon = player->pendingweapon == wp_nochange ? player->readyweapon : player->pendingweapon;
        }

        // Previous weapon, mapped to 1P button 4.
        if (pressed.player1.button4)
        {
            // Gotta figure out what weapon to try to swap to.
            if (player != NULL)
            {
                int weaponPos = _weapon_to_position(curWeapon);
                for (int try = 0; try < 6; try++)
                {
                    // Try to switch to the previous available weapon.
                    weaponPos --;
                    if (weaponPos < 0) { weaponPos = 6; }

                    // See if the player can switch to this weapon.
                    if (P_PlayerCanSwitchWeapon(player, _position_to_weapon(weaponPos)))
                    {
                        // They can! Simulate a press of that position.
                        printf("Pressing key %c\n", '1' + weaponPos);
                        I_SendInput(ev_keydown, '1' + weaponPos);
                        I_SendDelayedInput(ev_keyup, '1' + weaponPos);
                        break;
                    }
                }
            }
        }

        // Next weapon, mapped to 1P button 5.
        if (pressed.player1.button5)
        {
            // Gotta figure out what weapon to try to swap to.
            if (player != NULL)
            {
                int weaponPos = _weapon_to_position(curWeapon);
                for (int try = 0; try < 6; try++)
                {
                    // Try to switch to the next available weapon.
                    weaponPos ++;
                    if (weaponPos > 6) { weaponPos = 0; }

                    // See if the player can switch to this weapon.
                    if (P_PlayerCanSwitchWeapon(player, _position_to_weapon(weaponPos)))
                    {
                        // They can! Simulate a press of that position.
                        printf("Pressing key %c\n", '1' + weaponPos);
                        I_SendInput(ev_keydown, '1' + weaponPos);
                        I_SendDelayedInput(ev_keyup, '1' + weaponPos);
                        break;
                    }
                }
            }
        }

        // Automap modifier, mapped to 1P button 6.
        if (pressed.player1.button6)
        {
            I_SendInput(ev_keydown, KEY_TAB);
            I_SendDelayedInput(ev_keyup, KEY_TAB);
        }
        if (released.player1.button6)
        {
            I_SendInput(ev_keydown, KEY_TAB);
            I_SendDelayedInput(ev_keyup, KEY_TAB);
        }
    }

    // Safe to let video thread read inputs again.
    mutex_unlock(&control_mutex);
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

typedef struct
{
    int silent_attract;
    int show_messages;
    int show_options;
    int sfx_volume;
    int music_volume;
} doom_settings_t;

#define DOOM_EEPROM_VER 2
#define DOOM_EEPROM_VER1_SIZE 8
#define DOOM_EEPROM_VER2_SIZE 12

static int settings_loaded = 0;
static doom_settings_t settings;

doom_settings_t *_naomi_load_settings()
{
    // I'm not worried about race conditions here since I intend this only to
    // be used from the main Doom thread.
    if (settings_loaded == 0)
    {
        // Default settings.
        settings.silent_attract = 0;
        settings.music_volume = 8;
        settings.sfx_volume = 8;
        settings.show_messages = 1;
        settings.show_options = 1;

        eeprom_t eeprom;
        if (eeprom_read(&eeprom) == 0)
        {
            // Look up system setting for silent attract sounds, since the Naomi BIOS has
            // this setting built-in.
            settings.silent_attract = eeprom.system.attract_sounds == ATTRACT_SOUNDS_OFF;

            if (eeprom.game.size >= DOOM_EEPROM_VER1_SIZE)
            {
                if (memcmp(eeprom.game.data, "DOOM", 4) == 0)
                {
                    // Cool, let's figure out what version of data this is.
                    switch(eeprom.game.data[4])
                    {
                        case 2:
                        {
                            if (eeprom.game.size >= DOOM_EEPROM_VER2_SIZE)
                            {
                                settings.show_options = eeprom.game.data[8] ? 1 : 0;
                            }

                            // Fall-through to load other settings.
                        }
                        case 1:
                        {
                            if (eeprom.game.data[5] == 0 || eeprom.game.data[5] == 1)
                            {
                                settings.show_messages = eeprom.game.data[5];
                            }
                            if (eeprom.game.data[6] >= 0 && eeprom.game.data[6] <= 15)
                            {
                                settings.music_volume = eeprom.game.data[6];
                            }
                            if (eeprom.game.data[7] >= 0 && eeprom.game.data[7] <= 15)
                            {
                                settings.sfx_volume = eeprom.game.data[7];
                            }
                            break;
                        }
                    }
                }
            }
        }

        settings_loaded = 1;
    }

    return &settings;
}

void naomi_load_settings()
{
    // Just trigger an EEPROM load.
    _naomi_load_settings();
}

int naomi_get_silent_attract()
{
    doom_settings_t *cur_settings = _naomi_load_settings();
    return cur_settings->silent_attract;
}

int naomi_get_show_messages()
{
    doom_settings_t *cur_settings = _naomi_load_settings();
    return cur_settings->show_messages;
}

int naomi_get_sfx_volume()
{
    doom_settings_t *cur_settings = _naomi_load_settings();
    return cur_settings->sfx_volume;
}

int naomi_get_music_volume()
{
    doom_settings_t *cur_settings = _naomi_load_settings();
    return cur_settings->music_volume;
}

int naomi_get_show_options()
{
    doom_settings_t *cur_settings = _naomi_load_settings();
    return cur_settings->show_options;
}

void naomi_save_settings()
{
    eeprom_t eeprom;
    if (eeprom_read(&eeprom) == 0)
    {
        // Format the game settings.
        eeprom.game.size = DOOM_EEPROM_VER2_SIZE;
        memset(eeprom.game.data, 0, eeprom.game.size);
        memcpy(eeprom.game.data, "DOOM", 4);
        eeprom.game.data[4] = DOOM_EEPROM_VER;
        eeprom.game.data[5] = settings.show_messages;
        eeprom.game.data[6] = settings.music_volume;
        eeprom.game.data[7] = settings.sfx_volume;
        eeprom.game.data[8] = settings.show_options;

        // Write it back!
        eeprom_write(&eeprom);
    }

    // Since we overwrote settings, they're "loaded" now.
    settings_loaded = 1;
}

void naomi_set_show_messages(int val)
{
    settings_loaded = 1;
    settings.show_messages = val;
}

void naomi_set_sfx_volume(int val)
{
    settings_loaded = 1;
    settings.sfx_volume = val;
}

void naomi_set_music_volume(int val)
{
    settings_loaded = 1;
    settings.music_volume = val;
}

void naomi_set_show_options(int val)
{
    settings_loaded = 1;
    settings.show_options = val;
}

// Defined in d_main.c
extern char *wadfiles[MAXWADFILES];
void FindResponseFile(void);
void IdentifyVersion(void);

// Defined in i_naomi_video.c
void I_SetPalette(byte* palette);
void V_DrawChar(int x, int y, patch_t *patch, int dbl);
void V_DrawText(int x, int y, char *msg, ...);

// Defined in m_menu.c
extern short whichSkull;
extern char skullName[2][9];

#define SCREEN_MAIN 0
#define SCREEN_SETTINGS 1
#define SCREEN_CREDITS 2

int test()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // Make sure we have a mutex for control input ready.
    mutex_init(&control_mutex);

    // Set up error handling.
    memset(stderr_buf, 0, STDERR_LEN + 1);
    hook_stdio_calls( &error_hook );

    // Set up a test executable that uses parts of doom to display settings.
    video_init(VIDEO_COLOR_1555);
    video_set_background_color(rgb(0, 0, 0));

    // Then, enable console capture but disable console display if
    // we are not in debug mode.
    console_init(16);

#ifndef NAOMI_CONSOLE
    // Hide the debug console, we don't want to see it!
    console_set_visible(0);
#endif

    // Init our filesystem.
    if (romfs_init_default() != 0)
    {
        fprintf(stderr, "Failed to init filesystem!");
        I_DrawErrorScreen();
    }

    // Manually initialize what we need from doom.
    FindResponseFile();
    IdentifyVersion();
    Z_Init();
    W_InitMultipleFiles (wadfiles);
    HU_Init();
    M_Init();

    // This is a bit of a hack, but set the palette so we can do manual LUT.
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));

    // Load our EEPROM settings.
    naomi_load_settings();

    int screen = SCREEN_MAIN;
    int count = 0;
    int main_cursor = 0;
    int settings_cursor = 0;
    while ( 1 )
    {
        // First, poll the buttons and act accordingly.
        maple_poll_buttons();
        jvs_buttons_t buttons = maple_buttons_pressed();

        switch(screen)
        {
            case SCREEN_MAIN:
            {
                if (buttons.psw1 || buttons.test || buttons.player1.start || buttons.player2.start)
                {
                    switch(main_cursor)
                    {
                        case 0:
                        {
                            // Settings menu.
                            screen = SCREEN_SETTINGS;
                            settings_cursor = 0;
                            break;
                        }
                        case 2:
                        {
                            // Credits menu.
                            screen = SCREEN_CREDITS;
                            break;
                        }
                        case 4:
                        {
                            // Back to system test menu.
                            naomi_save_settings();
                            enter_test_mode();
                            break;
                        }
                    }
                }
                else if(buttons.psw2 || buttons.player1.service || buttons.player2.service || buttons.player1.down || buttons.player2.down)
                {
                    // Pop down to the next menu item if we're not at the bottom.
                    if (main_cursor < 4)
                    {
                        main_cursor += 2;
                    }
                    // Only wrap around if using svc to move.
                    else if (buttons.psw2 || buttons.player1.service || buttons.player2.service)
                    {
                        main_cursor = 0;
                    }
                }
                else if(buttons.player1.up || buttons.player2.up)
                {
                    // Pop up to the previous menu item if we're not at the top.
                    if (main_cursor > 0)
                    {
                        main_cursor -= 2;
                    }
                }

                // Display build date and version information.
                char *lines[] = {
                    "Settings",
                    "",
                    "Credits",
                    "",
                    "Exit",
                };

                // Draw it doom font style.
                int top = (video_height() - ((sizeof(lines) / sizeof(lines[0])) * 20)) / 2;
                for (int i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
                {
                    V_DrawText(100, top + (i * 20), lines[i]);

                    // Also draw the skull to show menu.
                    if (i == main_cursor)
                    {
                        V_DrawChar(70, top + (i * 20) - 3, W_CacheLumpName(skullName[whichSkull],PU_CACHE), 0);
                    }
                }
                break;
            }
            case SCREEN_SETTINGS:
            {
                if (buttons.psw1 || buttons.test || buttons.player1.start || buttons.player2.start)
                {
                    switch(settings_cursor)
                    {
                        case 0:
                        {
                            // Message setting
                            naomi_set_show_messages(1 - naomi_get_show_messages());
                            break;
                        }
                        case 2:
                        {
                            // SFX volume setting
                            if (naomi_get_sfx_volume() == 15)
                            {
                                naomi_set_sfx_volume(0);
                            }
                            else
                            {
                                naomi_set_sfx_volume(naomi_get_sfx_volume() + 1);
                            }
                            break;
                        }
                        case 4:
                        {
                            // Music volume setting
                            if (naomi_get_music_volume() == 15)
                            {
                                naomi_set_music_volume(0);
                            }
                            else
                            {
                                naomi_set_music_volume(naomi_get_music_volume() + 1);
                            }
                            break;
                        }
                        case 6:
                        {
                            // Options in game setting
                            naomi_set_show_options(1 - naomi_get_show_options());
                            break;
                        }
                        case 8:
                        {
                            // Exit
                            screen = SCREEN_MAIN;
                            break;
                        }
                    }
                }
                if (buttons.player1.left || buttons.player2.left)
                {
                    switch(settings_cursor)
                    {
                        case 0:
                        {
                            // Message setting
                            naomi_set_show_messages(1 - naomi_get_show_messages());
                            break;
                        }
                        case 2:
                        {
                            // SFX volume setting
                            if (naomi_get_sfx_volume() > 0)
                            {
                                naomi_set_sfx_volume(naomi_get_sfx_volume() - 1);
                            }
                            break;
                        }
                        case 4:
                        {
                            // Music volume setting
                            if (naomi_get_music_volume() > 0)
                            {
                                naomi_set_music_volume(naomi_get_music_volume() - 1);
                            }
                            break;
                        }
                        case 6:
                        {
                            // Options in game setting
                            naomi_set_show_options(1 - naomi_get_show_options());
                            break;
                        }
                    }
                }
                if (buttons.player1.right || buttons.player2.right)
                {
                    switch(settings_cursor)
                    {
                        case 0:
                        {
                            // Message setting
                            naomi_set_show_messages(1 - naomi_get_show_messages());
                            break;
                        }
                        case 2:
                        {
                            // SFX volume setting
                            if (naomi_get_sfx_volume() < 15)
                            {
                                naomi_set_sfx_volume(naomi_get_sfx_volume() + 1);
                            }
                            break;
                        }
                        case 4:
                        {
                            // Music volume setting
                            if (naomi_get_music_volume() < 15)
                            {
                                naomi_set_music_volume(naomi_get_music_volume() + 1);
                            }
                            break;
                        }
                        case 6:
                        {
                            // Options in game setting
                            naomi_set_show_options(1 - naomi_get_show_options());
                            break;
                        }
                    }
                }
                else if(buttons.psw2 || buttons.player1.service || buttons.player2.service || buttons.player1.down || buttons.player2.down)
                {
                    if (settings_cursor < 8)
                    {
                        settings_cursor += 2;
                    }
                    // Only wrap around if using svc to move.
                    else if (buttons.psw2 || buttons.player1.service || buttons.player2.service)
                    {
                        settings_cursor = 0;
                    }
                }
                else if(buttons.player1.up || buttons.player2.up)
                {
                    if (settings_cursor > 0)
                    {
                        settings_cursor -= 2;
                    }
                }

                // Display build date and version information.
                char *lines[] = {
                    "Messages: XXX",
                    "",
                    "SFX Volume: XX/XX",
                    "",
                    "Music Volume: XX/XX",
                    "",
                    "In Game Options: XXXXXX",
                    "",
                    "Exit",
                };

                // I tried to be clever here with strstr() but that gets screwed up
                // after the first loop.
                char *lineloc[] = {
                    lines[0] + 10,
                    lines[2] + 12,
                    lines[4] + 14,
                    lines[6] + 17,
                };

                // Hack to insert current setting.
                sprintf(lineloc[0], naomi_get_show_messages() ? "On" : "Off");
                sprintf(lineloc[1], "%d/15", naomi_get_sfx_volume());
                sprintf(lineloc[2], "%d/15", naomi_get_music_volume());
                sprintf(lineloc[3], naomi_get_show_options() ? "Shown" : "Hidden");

                // Draw it doom font style.
                int top = (video_height() - ((sizeof(lines) / sizeof(lines[0])) * 20)) / 2;
                for (int i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
                {
                    V_DrawText(100, top + (i * 20), lines[i]);

                    // Also draw the skull to show menu.
                    if (i == settings_cursor)
                    {
                        V_DrawChar(70, top + (i * 20) - 3, W_CacheLumpName(skullName[whichSkull],PU_CACHE), 0);
                    }
                }
                break;
            }
            case SCREEN_CREDITS:
            {
                if (buttons.psw1 || buttons.test || buttons.player1.start || buttons.player2.start)
                {
                    // Back to main menu.
                    screen = SCREEN_MAIN;
                }

                // Display build date and version information.
                char *lines[] = {
                    "Doom for the Sega Naomi",
                    "Ported by DragonMinded",
                    "",
                    "Build date: xxxx-xx-xx",
                    "Release version: 1.0 alpha 3",
                    "",
                    "Exit",
                };

                // Hack the build date into the correct line.
                int year = (BUILD_DATE / 10000);
                int month = (BUILD_DATE - (year * 10000)) / 100;
                int day = BUILD_DATE % 100;
                sprintf(&lines[3][12], "%04d-%02d-%02d", year, month, day);

                // Draw it doom font style.
                int top = (video_height() - ((sizeof(lines) / sizeof(lines[0])) * 20)) / 2;
                for (int i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
                {
                    V_DrawText(100, top + (i * 20), lines[i]);

                    // Also draw the skull to show menu.
                    if (i == 6)
                    {
                        V_DrawChar(70, top + (i * 20) - 3, W_CacheLumpName(skullName[whichSkull],PU_CACHE), 0);
                    }
                }
                break;
            }
        }

        // Display it!
        video_display_on_vblank();

        // Tick the menu skulls. Doom main loop is 35fps, we run at 60, so do a crude divide by 2.
        if (count++ & 1)
        {
            M_Ticker();
        }
    }
}
