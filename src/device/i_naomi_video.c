#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <naomi/video.h>
#include <naomi/color.h>
#include <naomi/ta.h>
#include <naomi/thread.h>
#include <naomi/interrupt.h>
#include <naomi/timer.h>
#include <naomi/maple.h>
#include <naomi/sprite/sprite.h>
#include "../v_video.h"
#include "../m_swap.h"
#include "../hu_stuff.h"

static uint32_t video_thread;
static texture_description_t *outtex[2];
static int whichtex = 0;
static float xscale;
static float yscale;
static int yoff;
static int debugxoff;
static int doom_updates;
static int started = 0;

// Shared with main.c
extern int controls_needed;
extern int controls_available;

// Shared with i_naomi_music.h
extern float percent_empty;
extern int m_volume;

// Shared with main.c
extern mutex_t control_mutex;

void _disableAnyVideoUpdates()
{
    thread_stop(video_thread);
}

void _enableAnyVideoUpdates()
{
    // Used to hide garbage on screen until the game
    // starts playing any music, signifying to us that
    // it has started running.
    started = 1;
}

void * video(void * param)
{
    int last_drawn_frame = -1;

#ifdef NAOMI_DEBUG
    double video_thread_fps = 0.0;
    double doom_fps = 0.0;
    uint32_t elapsed = 0;
    int video_updates = 0;
    int doom_last_reset = 0;
    task_scheduler_info_t sched;
#endif

    // Just display in a loop.
    while( 1 )
    {
#ifdef NAOMI_DEBUG
        // Calculate instantaneous video thread FPS, should hover around 60 FPS.
        int fps = profile_start();
#endif

        // Only draw to the texture if we got an update.
        if (started == 2 && last_drawn_frame != doom_updates)
        {
            // Remember that we did this.
            last_drawn_frame = doom_updates;

            // Now, request to draw the texture, making sure to scale it properly
            ta_commit_begin();
            sprite_draw_scaled(0, yoff, xscale, yscale, outtex[whichtex]);
            ta_commit_end();

            // Now, ask the TA to scale it for us
            ta_render();

#ifdef NAOMI_DEBUG
            video_draw_debug_text(debugxoff, 20, rgb(200, 200, 20), "Video FPS: %.01f, %dx%d", video_thread_fps, video_width(), video_height());
            video_draw_debug_text(debugxoff, 30, rgb(200, 200, 20), "DOOM FPS: %.01f, %dx%d", doom_fps, SCREENWIDTH, SCREENHEIGHT);
            video_draw_debug_text(debugxoff, 40, rgb(200, 200, 20), "Audio Buf Empty: %.01f%%", percent_empty * 100.0);
            video_draw_debug_text(debugxoff, 50, rgb(200, 200, 20), "Music Volume: %d/15", m_volume);
            video_draw_debug_text(debugxoff, 60, rgb(200, 200, 20), "IRQs: %lu", sched.interruptions);
            video_updates ++;
#endif

            // Now, display it on the next vblank
            video_display_on_vblank();
        }
        else
        {
            thread_wait_vblank_in();
        }

        // Now, poll for buttons where it is safe.
        mutex_lock(&control_mutex);
        if (controls_needed)
        {
            controls_needed = 0;
            controls_available = 1;
            maple_poll_buttons();
        }
        mutex_unlock(&control_mutex);

#ifdef NAOMI_DEBUG
        // Calculate instantaneous FPS.
        uint32_t uspf = profile_end(fps);
        video_thread_fps = 1000000.0 / (double)uspf;

        // Calculate DOOM and FPS based on requested screen updates.
        elapsed += uspf;
        if (elapsed >= 1000000)
        {
            int frame_count;
            ATOMIC({
                frame_count = (doom_updates - doom_last_reset);
                doom_last_reset = doom_updates;
            });

            doom_fps = (double)frame_count * ((double)elapsed / 1000000.0);
            elapsed = 0;
        }

        // Get task schduler info.
        task_scheduler_info(&sched);
#endif
    }
}

void I_InitGraphics (void)
{
    // Create a texture that we can use to render to to use hardware stretching.
    int uvsize = ta_round_uvsize(SCREENWIDTH > SCREENHEIGHT ? SCREENWIDTH : SCREENHEIGHT);
    outtex[0] = ta_texture_desc_malloc_paletted(uvsize, NULL, TA_PALETTE_CLUT8, 0);
    outtex[1] = ta_texture_desc_malloc_paletted(uvsize, NULL, TA_PALETTE_CLUT8, 0);
    whichtex = 0;

    // Wipe the textures so we don't have garbage on them.
    void *tmp = malloc(uvsize * uvsize);
    memset(tmp, 0, uvsize * uvsize);
    ta_texture_load(outtex[0]->vram_location, outtex[0]->width, 8, tmp);
    ta_texture_load(outtex[1]->vram_location, outtex[1]->width, 8, tmp);
    free(tmp);

    // Calculate the scaling factors and y offset. This is based off
    // of the assumption that doom wants to be stretched to a 4:3 resolution.
    if (video_is_vertical())
    {
        float yheight = ((float)video_width() * 3.0) / 4.0;
        xscale = (float)video_width() / (float)SCREENWIDTH;
        yscale = yheight / (float)SCREENHEIGHT;
        yoff = (video_height() - (int)yheight) / 2;
        debugxoff = 20;
    }
    else
    {
        xscale = (float)video_width() / (float)SCREENWIDTH;
        yscale = (float)video_height() / (float)SCREENHEIGHT;
        yoff = 0;
        debugxoff = 400;
    }

    // Mark that we don't have a frame.
    doom_updates = 0;
    controls_needed = 1;
    controls_available = 0;

    // Start video thread.
    video_thread = thread_create("video", video, NULL);
    thread_priority(video_thread, 1);
    thread_start(video_thread);
}

void I_ShutdownGraphics(void)
{
    thread_stop(video_thread);
    thread_destroy(video_thread);
    video_free();
}

void I_StartFrame (void)
{
    // Empty
}

void I_WaitVBL (int count)
{
    for (int i = 0; i < count; i++)
    {
        thread_wait_vblank_out();
        thread_yield();
    }
}

// Takes full 8 bit values.
void I_SetPalette (byte* palette)
{
    // set the X colormap entries
    uint32_t *bank = ta_palette_bank(TA_PALETTE_CLUT8, 0);

    for (int i = 0; i < 256; i++)
    {
        byte c = gammatable[usegamma][*palette++];
        color_t color;
        color.r = (c<<8) + c;
        c = gammatable[usegamma][*palette++];
        color.g = (c<<8) + c;
        c = gammatable[usegamma][*palette++];
        color.b = (c<<8) + c;
        color.a = 255;

        bank[i] = ta_palette_entry(color);
    }
}

void I_UpdateNoBlit (void)
{
    // Empty
}

void I_FinishUpdate (void)
{
    // Form the LUT texture.
    ta_texture_load_sprite(
        outtex[1 - whichtex]->vram_location,
        outtex[1 - whichtex]->width,
        8,
        0,
        0,
        SCREENWIDTH,
        SCREENHEIGHT,
        screens[0]
    );

    // We got audio, and we got an update finish.
    if (started == 1) { started = 2; }

    // Inform system that we have a new frame.
    ATOMIC({
        doom_updates++;
        whichtex = 1 - whichtex;
    });
}

void I_ReadScreen (byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

// Defined in hu_stuff.c
extern patch_t *hu_font[HU_FONTSIZE];

void V_DrawChar(int x, int y, patch_t *patch, int dbl)
{
    y -= (SHORT(patch->topoffset) * (dbl ? 2 : 1));
    x -= (SHORT(patch->leftoffset) * (dbl ? 2 : 1));

    uint32_t *bank = ta_palette_bank(TA_PALETTE_CLUT8, 0);
    int w = SHORT(patch->width);
    for (int col = 0; col < w; x += (dbl ? 2 : 1), col ++)
    {
        column_t *column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));

        // step through the posts in a column
        while (column->topdelta != 0xff)
        {
            byte *source = (byte *)column + 3;
            int count = column->length;
            int row = y + (column->topdelta) * (dbl ? 2 : 1);

            while (count--)
            {
                // Manually scale to 2x
                color_t color = ta_palette_reverse_entry(bank[*source++]);
                if (dbl)
                {
                    video_draw_pixel(x + 1, row, color);
                    video_draw_pixel(x, row++, color);
                    video_draw_pixel(x + 1, row, color);
                }
                video_draw_pixel(x, row++, color);
            }
            column = (column_t *)((byte *)column + column->length + 4);
        }
    }
}

void V_DrawText(int x, int y, char *msg, ...)
{
    if (msg)
    {
        char buffer[2048];
        va_list args;
        va_start(args, msg);
        int length = vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        for (int i = 0; i < length; i++)
        {
            int c = toupper(buffer[i]) - HU_FONTSTART;
            if (c < 0 || c> HU_FONTSIZE)
            {
                x += 8;
                continue;
            }

            int w = SHORT(hu_font[c]->width);
            V_DrawChar(x, y, hu_font[c], 1);
            x += (w * 2);
        }
    }
}
