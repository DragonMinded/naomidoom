#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <naomi/video.h>
#include <naomi/color.h>
#include <naomi/ta.h>
#include <naomi/thread.h>
#include <naomi/interrupt.h>
#include <naomi/timer.h>
#include <naomi/maple.h>
#include <naomi/sprite/sprite.h>
#include "../v_video.h"

static uint32_t video_thread;
static texture_description_t *outtex;
static uint8_t *tmptex;
static float xscale;
static float yscale;
static int doom_updates;

// Shared with main.c
extern int controls_needed;
extern int controls_available;

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
        if (last_drawn_frame != doom_updates)
        {
            last_drawn_frame = doom_updates;
            ta_texture_load(outtex->vram_location, outtex->width, 8, tmptex);

            // Now, request to draw the texture, making sure to scale it properly
            ta_commit_begin();
            sprite_draw_scaled(0, 0, xscale, yscale, outtex);
            ta_commit_end();

            // Now, ask the TA to scale it for us
            ta_render();

#ifdef NAOMI_DEBUG
            video_draw_debug_text(400, 20, rgb(200, 200, 20), "Video FPS: %.01f, %dx%d", video_thread_fps, video_width(), video_height());
            video_draw_debug_text(400, 30, rgb(200, 200, 20), "DOOM FPS: %.01f, %dx%d", doom_fps, SCREENWIDTH, SCREENHEIGHT);
            video_draw_debug_text(400, 40, rgb(200, 200, 20), "IRQs: %lu", sched.interruptions);
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
        ATOMIC({
            if (controls_needed)
            {
                controls_needed = 0;
                controls_available = 1;
                maple_poll_buttons();
            }
        });

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
    outtex = ta_texture_desc_malloc_paletted(uvsize, NULL, TA_PALETTE_CLUT8, 0);
    tmptex = malloc(sizeof(uint8_t) * outtex->width * outtex->height);
    memset(tmptex, 0, sizeof(uint8_t) * outtex->width * outtex->height);

    // Calculate the scaling factors.
    xscale = (float)video_width() / (float)SCREENWIDTH;
    yscale = (float)video_height() / (float)SCREENHEIGHT;

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
    for (int gy = 0; gy < SCREENHEIGHT; gy++)
    {
        memcpy(&tmptex[gy * outtex->width], screens[0] + (gy * SCREENWIDTH), SCREENWIDTH);
    }

    // Inform system that we have a new frame.
    ATOMIC(doom_updates++);
}

void I_ReadScreen (byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
