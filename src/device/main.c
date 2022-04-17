#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <naomi/romfs.h>
#include "../d_main.h"
#include "../m_argv.h"

int main()
{
    // Set up arguments.
    myargc = 1;
    myargv = malloc(sizeof (myargv[0]) * myargc);
    myargv[0] = strdup("doom.bin");

    // Init our filesystem.
    romfs_init_default();

    // Off we go!
    D_DoomMain();

    return 0;
}

void I_StartTic (void)
{
    // Empty
}

void mkdir(const char *path, int perm)
{
    // Empty
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
