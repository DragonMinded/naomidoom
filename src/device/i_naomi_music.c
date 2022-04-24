#include <malloc.h>
#include <timidity.h>
#include <naomi/audio.h>
#include <naomi/thread.h>
#include "../i_sound.h"

#define INVALID_HANDLE -1
#define BUFSIZE 4096
#define SAMPLERATE 22050

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
static volatile int m_volume = 127;

static registered_music_t *reglist = 0;
static int reglist_count = 0;
static int global_count = 1;

static play_instructions_t instructions;

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
    options.buffer_size = BUFSIZE / 4;

    // Now that we're ready to go, set our priority high so we don't stutter.
    thread_priority(instructions.thread, 1);

    MidSong *song = mid_song_load (stream, &options);
    mid_istream_close (stream);

    if (song == NULL)
    {
        return NULL;
    }

    uint32_t *buffer = malloc(BUFSIZE);
    mid_song_set_volume(song, m_volume * 4);
    mid_song_start(song);

    int sleep_us = (int)(1000000.0 * (((float)BUFSIZE / 4.0) / (float)SAMPLERATE));
    int volume = m_volume;

    audio_register_ringbuffer(AUDIO_FORMAT_16BIT, SAMPLERATE, BUFSIZE);

    while (inst->exit == 0)
    {
        int bytes_read;
        while (inst->exit == 0 && (bytes_read = mid_song_read_wave(song, (void *)buffer, BUFSIZE)))
        {
            int numsamples = bytes_read / 4;
            uint32_t *samples = buffer;

            while (numsamples > 0 && inst->exit == 0)
            {
                while (inst->pause != 0 && inst->exit == 0)
                {
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
                    if (actual_written < numsamples)
                    {
                        numsamples -= actual_written;
                        samples += actual_written;

                        // Sleep for the time it takes to play half our buffer so we can wake up and
                        // fill it again.
                        thread_sleep(sleep_us);

                        // Allow volume changes.
                        if (volume != m_volume)
                        {
                            volume = m_volume;
                            mid_song_set_volume(song, volume);
                        }
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
    free(buffer);

    return 0;
}

void I_InitMusic(void)
{
    if (mid_init ("rom://timidity/timidity.cfg") < 0)
    {
        return;
    }

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
    }

    m_initialized = 0;
}

// Volume.
void I_SetMusicVolume(int volume)
{
    m_volume = volume < 0 ? 0 : volume;
    m_volume = m_volume > 127 ? 127 : m_volume;
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
