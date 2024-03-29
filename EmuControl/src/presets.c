#include <proto/exec.h>
#include <libraries/iffparse.h>
#include <utility/tagitem.h>
#include <clib/alib_protos.h>
#include <proto/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdarg.h>
#include <stdint.h>

#include "presets.h"

#define ID_EMUC MAKE_ID('E', 'm', 'u', 'C')
#define ID_PREF MAKE_ID('P', 'R', 'E', 'F')
#define ID_PRHD MAKE_ID('P', 'R', 'H', 'D')

struct PrefHeader
{
    UBYTE ph_Version;
    UBYTE ph_Type;
    ULONG ph_Flags;
} prhd = { 2, 0, 0 };

const char default_dir[] = "SYS:Prefs/Presets";

int SavePreset(struct Preset * preset, char *name, char *path)
{
    char dirname[256];
    int retval = 0;
    struct Library * IFFParseBase = OpenLibrary("iffparse.library", 0);

    if (IFFParseBase != NULL)
    {
        // If no path was given, copy default path first, otherwise copy specified path
        if (path == NULL) {
            CopyMem((void*)default_dir, dirname, sizeof(default_dir));
        } else {
            int n=0;
            do {
                dirname[n++] = *path++;
            } while(*path && n < (sizeof(dirname) - 1));
            dirname[n] = 0;
        }

        // Append file name. If it was given as absolute path, it will overwrite default dir
        if (AddPart(dirname, name, sizeof(dirname)) == DOSFALSE)
        {
            CloseLibrary(IFFParseBase);
            return 0;
        }

        struct IFFHandle *iff = AllocIFF();

        if (iff != NULL)
        {
            iff->iff_Stream = Open(dirname, MODE_NEWFILE);

            if (iff->iff_Stream)
            {
                InitIFFasDOS(iff);
                OpenIFF(iff, IFFF_WRITE);

                PushChunk(iff, ID_PREF, ID_FORM, IFFSIZE_UNKNOWN);

                PushChunk(iff, ID_PREF, ID_PRHD, sizeof(struct PrefHeader));
                WriteChunkBytes(iff, &prhd, sizeof(prhd));
                PopChunk(iff);


                PushChunk(iff, ID_PREF, ID_EMUC, sizeof(struct Preset));
                WriteChunkBytes(iff, preset, sizeof(struct Preset));
                PopChunk(iff);

                PopChunk(iff);

                CloseIFF(iff);
                Close(iff->iff_Stream);

                retval = 1;
            }

            FreeIFF(iff);
        }

        CloseLibrary(IFFParseBase);
    }

    return retval;
}

int LoadPreset(struct Preset * preset, char * name, char *path)
{
    char dirname[256];
    int retval = 0;
    struct Library * IFFParseBase = OpenLibrary("iffparse.library", 0);

    if (IFFParseBase != NULL)
    {
        // If no path was given, copy default path first, otherwise copy specified path
        if (path == NULL) {
            CopyMem((void*)default_dir, dirname, sizeof(default_dir));
        } else {
            int n=0;
            do {
                dirname[n++] = *path++;
            } while(*path && n < (sizeof(dirname) - 1));
            dirname[n] = 0;
        }

        // Append file name. If it was given as absolute path, it will overwrite default dir
        if (AddPart(dirname, name, sizeof(dirname)) == DOSFALSE)
        {
            CloseLibrary(IFFParseBase);
            return 0;
        }

        struct IFFHandle *iff = AllocIFF();

        if (iff != NULL)
        {
            iff->iff_Stream = Open(dirname, MODE_OLDFILE);

            if (iff->iff_Stream)
            {
                InitIFFasDOS(iff);
                OpenIFF(iff, IFFF_READ);

                PropChunk(iff, ID_PREF, ID_PRHD);
                StopChunk(iff, ID_PREF, ID_EMUC);
                ParseIFF(iff, IFFPARSE_SCAN);

                struct StoredProperty *sp = FindProp(iff, ID_PREF, ID_PRHD);

                if (sp)
                {
                    struct PrefHeader *pf = (struct PrefHeader *)sp->sp_Data;
                    ReadChunkBytes(iff, preset, sizeof(struct Preset));
                    /* For version 1 of the header set SlowChipSpacing to 0xff which disables changing it */
                    if (pf->ph_Version == 1)
                    {
                        preset->pr_SlowChipSpacing = 0xff;
                    }
                    retval = 1;
                }

                CloseIFF(iff);
                Close(iff->iff_Stream);
            }

            FreeIFF(iff);
        }

        CloseLibrary(IFFParseBase);
    }

    return retval;
}
