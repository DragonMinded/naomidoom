#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <naomi/video.h>
#include <naomi/color.h>
#include <naomi/console.h>
#include <naomi/ta.h>
#include <naomi/thread.h>
#include <naomi/sprite/sprite.h>
#include "../v_video.h"

static uint32_t video_thread;
static texture_description_t *outtex;
static uint8_t *tmptex;
static float xscale;
static float yscale;

void * video(void * param)
{
    // Just display in a loop.
    while( 1 )
    {
        ta_texture_load(outtex->vram_location, outtex->width, 8, tmptex);

        // Now, request to draw the texture, making sure to scale it properly
        ta_commit_begin();
        sprite_draw_scaled(0, 0, xscale, yscale, outtex);
        ta_commit_end();

        // Now, ask the TA to scale it for us
        ta_render();

        // Now, display it on the next vblank
        video_display_on_vblank();
    }
}

void I_InitGraphics (void)
{
    // First, initialize a simple screen.
    video_init(VIDEO_COLOR_1555);

    // Then, enable console capture but disable console display.
    console_init(16);
    console_set_visible(0);

    // Now, create a texture that we can use to render to to use hardware stretching.
    int uvsize = ta_round_uvsize(SCREENWIDTH > SCREENHEIGHT ? SCREENWIDTH : SCREENHEIGHT);
    outtex = ta_texture_desc_malloc_paletted(uvsize, NULL, TA_PALETTE_CLUT8, 0);
    tmptex = malloc(sizeof(uint8_t) * outtex->width * outtex->height);

    // Calcualte the scaling factors.
    xscale = (float)video_width() / (float)SCREENWIDTH;
    yscale = (float)video_height() / (float)SCREENHEIGHT;

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
}

void I_ReadScreen (byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
