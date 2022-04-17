#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <naomi/video.h>
#include <naomi/color.h>
#include <naomi/console.h>
#include "../v_video.h"


void I_InitGraphics (void)
{
    video_init(VIDEO_COLOR_1555);
    console_init(16);
    console_set_visible(0);
}

void I_ShutdownGraphics(void)
{
    video_free();
}

void I_StartFrame (void)
{
    // Empty??
}

void I_WaitVBL (void)
{
    // Empty??
}

static color_t colors[256];

// Takes full 8 bit values.
void I_SetPalette (byte* palette)
{
    // set the X colormap entries
    for (int i = 0; i < 256; i++)
    {
        byte c = gammatable[usegamma][*palette++];
        colors[i].r = (c<<8) + c;
        c = gammatable[usegamma][*palette++];
        colors[i].g = (c<<8) + c;
        c = gammatable[usegamma][*palette++];
        colors[i].b = (c<<8) + c;
        colors[i].a = 255;
    }
}

void I_UpdateNoBlit (void)
{
    // Empty??
}

void I_FinishUpdate (void)
{
    // TODO: We should be copying this to a texture so we can use hardware stretching.
    for (int gy = 0; gy < SCREENHEIGHT; gy++)
    {
        for (int gx = 0; gx < SCREENWIDTH; gx++)
        {
            video_draw_pixel(gx, gy, colors[*(screens[0]+gy*SCREENWIDTH+gx)]);
        }
    }

    video_display_on_vblank();
}

void I_ReadScreen (byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}
