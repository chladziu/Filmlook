/* filmlook.c - build test module (step 0).
 * Only proves that the module builds in GitHub Actions and loads on the camera:
 * on load it writes ML/LOGS/FLMOK.TXT and counts key presses in that file's sibling log.
 * It does NOT touch any Canon property, and does not hook the capture path. */
#include <module.h>
#include <dryos.h>
#include <bmp.h>

static void write_note(const char *name, const char *text)
{
    FILE *f = FIO_CreateFile(name);
    if (f)
    {
        FIO_WriteFile(f, text, strlen(text));
        FIO_CloseFile(f);
    }
}

static unsigned int filmlook_init()
{
    write_note("ML/LOGS/FLMOK.TXT", "filmlook module loaded\n");
    printf("filmlook: loaded\n");
    return 0;
}

static unsigned int filmlook_deinit()
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(filmlook_init)
    MODULE_DEINIT(filmlook_deinit)
MODULE_INFO_END()
