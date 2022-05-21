#include <malloc.h>
#include <timidity.h>
#include <naomi/audio.h>
#include <naomi/thread.h>
#include "../i_sound.h"

#define INVALID_HANDLE -1
#define SAMPLELENGTH 16384
#define SILENCELENGTH 2048
#define SAMPLERATE 44100
#define MAX_VOLUME 15

typedef struct
{
    int handle;
    void *data;
    int size;
} registered_music_t;

typedef struct
{
    // Info about current music playing.
    int handle;
    void *data;
    int size;

    // Control variables.
    uint32_t thread;
    volatile int pause;
    volatile int exit;
    int loop;
} play_instructions_t;

static int m_initialized = 0;
int m_volume = 15;

static registered_music_t *reglist = 0;
static int reglist_count = 0;
static int global_count = 1;

static play_instructions_t instructions;
static uint32_t *buffer;
static uint32_t *silence;

// How many samples out of the total did we write last wake-up.
float percent_empty = 0.0;

// Specifically so errors aren't annoying to display.
void _pauseAnySong()
{
    instructions.pause = 1;
}

// Logarithmic volume levels for sounds.
static float logtable[16] = {
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

void *audiothread_music(void *param)
{
    play_instructions_t *inst = (play_instructions_t *)param;

    MidIStream *stream = mid_istream_open_mem (inst->data, inst->size);
    if (stream == NULL)
    {
        return NULL;
    }

    MidSongOptions options;
    options.rate = SAMPLERATE;
    options.format = MID_AUDIO_S16LSB;
    options.channels = 2;
    options.buffer_size = SAMPLELENGTH;

    // Now that we're ready to go, set our priority high so we don't stutter.
    thread_priority(instructions.thread, 2);

    MidSong *song = mid_song_load (stream, &options);
    mid_istream_close (stream);

    if (song == NULL)
    {
        return NULL;
    }

    mid_song_set_volume(song, 150);
    mid_song_start(song);

    // Specifically want to wake up before its our time to fill the buffer again,
    // so we leave ourselves room for 1/4 of the buffer to have filled. If you
    // turn on debugging, you should see the buf empty percent hover around 25%.
    int sleep_us = (int)(1000000.0 * ((float)SAMPLELENGTH / (float)SAMPLERATE) * (1.0 / 4.0));
    int written = 0;

    audio_register_ringbuffer(AUDIO_FORMAT_16BIT, SAMPLERATE, SAMPLELENGTH);
    audio_change_ringbuffer_volume(logtable[m_volume]);

    while (inst->exit == 0)
    {
        int bytes_read;
        while (inst->exit == 0 && (bytes_read = mid_song_read_wave(song, (void *)buffer, SAMPLELENGTH * 4)))
        {
            int numsamples = bytes_read / 4;
            uint32_t *samples = buffer;

            while (numsamples > 0 && inst->exit == 0)
            {
                while (inst->pause != 0 && inst->exit == 0)
                {
                    // Write empty silence until the buffer is full.
                    int written_this_loop;
                    while((written_this_loop = audio_write_stereo_data(silence, SILENCELENGTH)) == SILENCELENGTH) { written += written_this_loop; }
                    written += written_this_loop;

                    // Keep track of how many samples we actually wrote (buffer empty %).
                    percent_empty = (float)written / (float)SAMPLELENGTH;
                    written = 0;

                    // Sleep for an arbitrary amount and check again.
                    thread_sleep(sleep_us);
                }

                if (inst->exit == 0)
                {
                    int actual_written = audio_write_stereo_data(samples, numsamples);
                    if (actual_written < 0)
                    {
                        // Uh oh!
                        inst->exit = 1;
                        break;
                    }

                    // Purely for debugging, to see how full/empty the buffer is staying.
                    written += actual_written;

                    if (actual_written < numsamples)
                    {
                        numsamples -= actual_written;
                        samples += actual_written;

                        // Keep track of how many samples we actually wrote (buffer empty %).
                        percent_empty = (float)written / (float)SAMPLELENGTH;
                        written = 0;

                        // Sleep for the time it takes to play half our buffer so we can wake up and
                        // fill it again.
                        thread_sleep(sleep_us);
                    }
                    else
                    {
                        numsamples = 0;
                    }
                }
            }
        }

        if (inst->exit == 0)
        {
            if (inst->loop == 0)
            {
                // We aren't looping.
                break;
            }

            mid_song_start(song);
        }
    }

    audio_unregister_ringbuffer();
    mid_song_free (song);

    return 0;
}

void I_InitMusic(void)
{
    if (mid_init ("rom://timidity/timidity.cfg") < 0)
    {
        return;
    }

    buffer = malloc(SAMPLELENGTH * 4);
    silence = malloc(SILENCELENGTH * 4);
    memset(silence, 0, SILENCELENGTH * 4);

    m_initialized = 1;
    reglist_count = 0;
    instructions.handle = INVALID_HANDLE;
}

void I_ShutdownMusic(void)
{
    if (m_initialized)
    {
        // Need to shut down any threads or current playing.
        if (instructions.handle != INVALID_HANDLE)
        {
            I_StopSong(instructions.handle);
        }

        mid_exit();

        if (reglist)
        {
            // Need to free any data that's still valid on this list.
            for (int i = 0; i < reglist_count; i++)
            {
                if (reglist[i].handle != INVALID_HANDLE)
                {
                    reglist[i].handle = INVALID_HANDLE;
                    free(reglist[i].data);
                }
            }

            free(reglist);
        }

        reglist = 0;
        reglist_count = 0;

        free(buffer);
        free(silence);
    }

    m_initialized = 0;
}

// Volume.
void I_SetMusicVolume(int volume)
{
    m_volume = volume < 0 ? 0 : volume;
    m_volume = m_volume > 15 ? 15 : m_volume;
    audio_change_ringbuffer_volume(logtable[m_volume]);
}

// PAUSE game handling.
void I_PauseSong(int handle)
{
    if (!m_initialized) { return; }

    if (handle == instructions.handle)
    {
        instructions.pause = 1;
    }
}

void I_ResumeSong(int handle)
{
    if (!m_initialized) { return; }

    if (handle == instructions.handle)
    {
        instructions.pause = 0;
    }
}

// Prototype so we don't have to pull in everything in the convert header.
int convertToMidi(void *musData, void **midiOutput, int *midiSize);

// Registers a song handle to song data.
int I_RegisterSong(void *data, const char *name)
{
    if (!m_initialized) { return 0; }

    void *midiData = 0;
    int midiSize = 0;
    if (!convertToMidi(data, &midiData, &midiSize))
    {
        return 0;
    }

    // Need to register this in our internal list.
    int new_handle = global_count++;
    for (int i = 0; i < reglist_count; i++)
    {
        if (reglist[i].handle == INVALID_HANDLE)
        {
            reglist[i].handle = new_handle;
            reglist[i].data = midiData;
            reglist[i].size = midiSize;
            return new_handle;
        }
    }

    // Need to allocate a new one.
    if (reglist_count == 0)
    {
        reglist = malloc(sizeof(*reglist));
        if (!reglist) { return 0; }
    }
    else
    {
        registered_music_t * newreglist = realloc(reglist, sizeof(*reglist) * (reglist_count + 1));
        if (!newreglist) { return 0; }
        reglist = newreglist;
    }

    reglist[reglist_count].handle = new_handle;
    reglist[reglist_count].data = midiData;
    reglist[reglist_count].size = midiSize;
    reglist_count ++;

    return new_handle;
}

// Forward definition from video system.
void _enableAnyVideoUpdates();

// Called by anything that wishes to start music.
//  plays a song, and when the song is done,
//  starts playing it again in an endless loop.
// Horrible thing to do, considering.
void I_PlaySong(int	handle, int	looping)
{
    if (!m_initialized) { return; }

    // We don't support playing multiple songs at once.
    if (instructions.handle != INVALID_HANDLE)
    {
        return;
    }

    // Signal that we have life from the main game.
    _enableAnyVideoUpdates();

    for (int i = 0; i < reglist_count; i++)
    {
        if (reglist[i].handle == handle)
        {
            instructions.handle = handle;
            instructions.data = reglist[i].data;
            instructions.size = reglist[i].size;
            instructions.pause = 0;
            instructions.exit = 0;
            instructions.loop = looping;

            instructions.thread = thread_create("music", &audiothread_music, &instructions);
            thread_start(instructions.thread);

            return;
        }
    }
}

// Stops a song over 3 seconds.
void I_StopSong(int handle)
{
    if (!m_initialized) { return; }

    // We don't support playing multiple songs at once.
    if (instructions.handle != handle)
    {
        return;
    }

    instructions.exit = 1;
    thread_join(instructions.thread);
    thread_destroy(instructions.thread);

    // Mark that we aren't playing anything.
    instructions.handle = INVALID_HANDLE;
}

// See above (register), then think backwards
void I_UnRegisterSong(int handle)
{
    if (!m_initialized) { return; }

    for (int i = 0; i < reglist_count; i++)
    {
        if (reglist[i].handle == handle)
        {
            reglist[i].handle = INVALID_HANDLE;
            free(reglist[i].data);
            return;
        }
    }
}
