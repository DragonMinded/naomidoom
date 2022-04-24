#include <stdlib.h>
#include <naomi/audio.h>
#include "../i_sound.h"
#include "../w_wad.h"
#include "../z_zone.h"


// So we can link between SFX and AICA-registered sound effects.
typedef struct {
    sfxinfo_t *sfx;
    int handle;
} audio_link_t;

audio_link_t links[NUMSFX];

static void _load_sfx(sfxinfo_t *sfxInfo)
{
    // First, grab the lump itself.
    int lumpNum = I_GetSfxLumpNum(sfxInfo);

    // Grab the size of the SFX.
    int soundSize = W_LumpLength(lumpNum);
    if (soundSize <= 8 || soundSize >= (65535 + 8))
    {
        printf("Sound effect %s is empty or too large?\n", sfxInfo->name);
        sfxInfo->data = NULL;
        sfxInfo->length = 0;
    }

    // Grab actual SFX data, ignore the header.
    unsigned char *data = W_CacheLumpNum(lumpNum, PU_STATIC);
    unsigned char *converted = malloc(soundSize - 8);

    for (int i = 0; i < soundSize - 8; i++)
    {
        // Convert unsigned to signed.
        converted[i] = data[i + 8] ^ 0x80;
    }

    // No longer need WAD data.
    Z_Free (data);

    // Set it!
    sfxInfo->data = converted;
    sfxInfo->length = soundSize - 8;
}

void I_InitSound()
{
    printf("I_InitSound()\n");

    // Load all sounds, stick them into the AICA for instant playback.
    for (int i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link)
        {
            // Normal sound file, load from WAD.
            _load_sfx(&S_sfx[i]);
        }
        else
        {
            // Link to another sound file, just use that link. Malloc so we
            // can safely free later.
            S_sfx[i].data = malloc(S_sfx[i].link->length);
            memcpy(S_sfx[i].data, S_sfx[i].link->data, S_sfx[i].link->length);
            S_sfx[i].length = S_sfx[i].link->length;
        }

        // Now, register the audio with the AICA.
        if (S_sfx[i].data)
        {
            links[i].sfx = &S_sfx[i];
            links[i].handle = audio_register_sound(AUDIO_FORMAT_8BIT, 11025, S_sfx[i].data, S_sfx[i].length);
            printf("Registered %s as AICA handle %d\n", S_sfx[i].name, links[i].handle);
        }
        else
        {
            links[i].sfx = &S_sfx[i];
            links[i].handle = -1;
        }
    }
}

void I_UpdateSound(void)
{
    // Empty.
}

void I_SubmitSound(void)
{
    // Empty.
}

void I_ShutdownSound(void)
{
    for (int i = 1; i < NUMSFX; i++)
    {
        free(S_sfx[i].data);
        links[i].sfx = NULL;

        audio_unregister_sound(links[i].handle);
        links[i].handle = 0;
    }
}

void I_SetChannels()
{
    // Empty.
}

int I_GetSfxLumpNum (sfxinfo_t* sfxinfo)
{
    // Given the name, construct the sound name in the WAD.
    char wadName[16];
    snprintf(wadName, 15, "ds%s", sfxinfo->name);

    if (W_CheckNumForName(wadName) < 0)
    {
        return W_GetNumForName("dspistol");
    }
    else
    {
        return W_GetNumForName(wadName);
    }
}

// Logarithmic volume levels for sounds.
float logtable[16] = {
    0.000,
    0.097,
    0.273,
    0.398,
    0.495,
    0.574,
    0.641,
    0.699,
    0.750,
    0.796,
    0.837,
    0.875,
    0.910,
    0.942,
    0.972,
    1.000,
};

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    if (links[id].sfx != NULL && links[id].handle > 0)
    {
        if (vol < 0) { vol = 0; }
        if (vol > 15) { vol = 15; }

        // TODO: Stereo separation, by starting the same sound at different volumes on each speaker.
        audio_play_registered_sound(links[id].handle, SPEAKER_LEFT | SPEAKER_RIGHT, logtable[vol]);
        return links[id].handle;
    }

    return 0;
}

void I_StopSound(int handle)
{
    if (handle > 0)
    {
        // This technically stops all instances of this sound, however it seems to work fine.
        audio_stop_registered_sound(handle);
    }
}

int I_SoundIsPlaying(int handle)
{
    // TODO: We need to return that the sound is still playing for any sound, so
    // that the game will attempt to modify it if needed.
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    // TODO: libnaomi doesn't let us modify in-progress sounds yet, so we can't
    // do anything with this.
    return;
}
