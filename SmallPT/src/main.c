/*
    Copyright 2017, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <graphics/gfx.h>
#include <exec/tasks.h>
#include <exec/ports.h>
#include <exec/lists.h>
#include <libraries/Picasso96.h>

#include <proto/Picasso96.h>

#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <clib/timer_protos.h>

#include "renderer.h"
#include "support.h"

CONST_STRPTR version = "$VER: SMP-Smallpt 1.0 (03.03.2017) �2017 The AROS Development Team";

struct Window * createMainWindow(int req_width, int req_height)
{
    struct Screen *pubScreen;
    struct Window *displayWin = NULL;
    int width, height;

    pubScreen = LockPubScreen(0);

    if (pubScreen)
    {
        width = ((pubScreen->Width * 4) / 5) & ~0x1f;
        height = (width * 3 / 4) & ~0x1f;

        if (req_width && req_width < width)
            width = req_width & ~0x1f;
        if (req_height && req_height < height)
            height = req_height & ~0x1f;

        if (height >= (pubScreen->Height * 4) / 5)
        {
            height = ((pubScreen->Height * 4) / 5) & ~0x1f;
            width = (height * 4 / 3) & ~0x1f;
        }
    }
    else
    {
        width = 320;
        height = 240;
    }

    if ((displayWin = OpenWindowTags(0,
                                     WA_PubScreen, (Tag)pubScreen,
                                     WA_Left, 0,
                                     WA_Top, (pubScreen) ? pubScreen->BarHeight : 10,
                                     WA_InnerWidth, width,
                                     WA_InnerHeight, height,
                                     WA_Title, (Tag) "SMP-Smallpt renderer",
                                     WA_SimpleRefresh, TRUE,
                                     WA_CloseGadget, TRUE,
                                     WA_DepthGadget, TRUE,
                                     WA_DragBar, TRUE,
                                     WA_SizeGadget, FALSE,
                                     WA_SizeBBottom, FALSE,
                                     WA_SizeBRight, FALSE,
                                     WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
                                     TAG_DONE)) != NULL)
    {
        if (pubScreen)
            UnlockPubScreen(0, pubScreen);
    }

    return displayWin;
}

#define ARG_TEMPLATE "THREADS/K/N,MAXITER/K/N,WIDTH/K/N,HEIGHT/K/N,RAYDEPTH/K/N,EXPLICIT/S"

enum {
    ARG_MAXCPU,
    ARG_MAXITER,
    ARG_WIDTH,
    ARG_HEIGHT,
    ARG_RAYDEPTH,
    ARG_EXPLICIT,
    
    ARG_COUNT
};

struct TimerBase *TimerBase = NULL;
struct Library * P96Base = NULL;

int main()
{
    APTR ProcessorBase;
    ULONG args[ARG_COUNT] = { 0, 0, 0, 0, 0, 0, };
    struct RDArgs *rda;
    int max_cpus = 1;
    int max_iter = 8;
    int req_width = 320, req_height = 240;
    char tmpbuf[200];
    int explicit_mode = 0;
    struct MsgPort *timerPort = CreateMsgPort();
    struct timerequest *tr = CreateIORequest(timerPort, sizeof(struct timerequest));

    struct timeval start_time;
    struct timeval now;

    struct Window *displayWin;
    struct BitMap *outputBMap = NULL;

    ULONG coreCount = 1;

    SetTaskPri(FindTask(NULL), 5);

    rda = ReadArgs(ARG_TEMPLATE, args, NULL);
    if (rda != NULL)
    {
        LONG *ptr = (LONG *)args[ARG_MAXCPU];
        if (ptr)
            max_cpus = *ptr;

        ptr = (LONG *)args[ARG_RAYDEPTH];
        if (ptr) {
            maximal_ray_depth = *ptr;
            if (maximal_ray_depth < 2)
                maximal_ray_depth = 2;
            if (maximal_ray_depth > 1000)
                maximal_ray_depth = 1000;
        }

        ptr = (LONG *)args[ARG_MAXITER];
        if (ptr)
        {
            max_iter = *ptr;
            if (max_iter < 1)
                max_iter = 1;
            else if (max_iter > 10000)
                max_iter = 10000;
        }

        ptr = (LONG *)args[ARG_WIDTH];
        if (ptr)
            req_width = *ptr;

        if (req_width && req_width < 160)
            req_width = 160;

        ptr = (LONG *)args[ARG_HEIGHT];
        if (ptr)
            req_height = *ptr;

        if (req_height && req_height < 128)
            req_height = 128;

        if (max_iter == 0)
            max_iter = 16;

        explicit_mode = args[ARG_EXPLICIT];
    }

    P96Base = OpenLibrary("Picasso96API.library", 0);

    if (!P96Base) {
        Printf("Failed to open Picasso96API.library!\n"); 
        return -1;
    }

    if (timerPort)
    {
        FreeSignal(timerPort->mp_SigBit);
        timerPort->mp_SigBit = -1;
        timerPort->mp_Flags = PA_IGNORE;
    }

    if (tr)
    {
        if (!OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)tr, 0))
        {
            TimerBase = (struct TimerBase *)tr->tr_node.io_Device;
        }
    } else return 0;

    coreCount = max_cpus;

    displayWin = createMainWindow(req_width, req_height);

    if (displayWin)
    {
        int width, height;
        struct RastPort *outBMRastPort;
        ULONG *workBuffer;
        int windowClosing = FALSE;
        struct MsgPort *mainPort = CreateMsgPort();
        struct MsgPort *rendererPort = NULL;
        ULONG signals;
        struct Task *renderer;
        struct Message *msg;
        struct MyMessage cmd;
        BOOL busyPointer = FALSE;
        int tasksWork = 0;
        int tasksIn = 0;
        int tasksOut = 0;

        width = (displayWin->Width - displayWin->BorderLeft - displayWin->BorderRight);
        height = (displayWin->Height - displayWin->BorderTop - displayWin->BorderBottom);

        Printf("Created window with inner size of %ldx%ld\n", width, height);
        Printf("Tiles amount %ldx%ld\n", width / 32, height / 32);
        if (explicit_mode)
            Printf("Explicit mode enabled\n");
        Printf("Number of threads: %ld\n", coreCount);
        Printf("Ray depth: %ld\n", maximal_ray_depth);
        Printf("Number of iterations: %ld\n", max_iter);

        outputBMap = AllocBitMap(
                        width,
                        height,
                        GetBitMapAttr(displayWin->WScreen->RastPort.BitMap, BMA_DEPTH),
                        BMF_DISPLAYABLE, displayWin->WScreen->RastPort.BitMap);

        outBMRastPort = (struct RastPort *)AllocMem(sizeof(struct RastPort), MEMF_ANY);
        InitRastPort(outBMRastPort);
        outBMRastPort->BitMap = outputBMap;

        workBuffer = AllocMem(width * height * sizeof(ULONG), MEMF_ANY | MEMF_CLEAR);

        struct RenderInfo ri;
        ri.Memory = workBuffer;
        ri.BytesPerRow = width * sizeof(ULONG);
        ri.RGBFormat = RGBFB_R8G8B8A8;

        p96WritePixelArray(&ri, 0, 0, outBMRastPort, 0, 0, width, height);

        BltBitMapRastPort (outputBMap, 0, 0,
            displayWin->RPort, displayWin->BorderLeft, displayWin->BorderTop,
            width, height, 0xC0); 

        Printf("Creating renderer task\n");

        renderer = NewCreateTask(TASKTAG_NAME,      (Tag)"SMP-Smallpt Master",
                                TASKTAG_PRI,        3,
                                TASKTAG_PC,         (Tag)Renderer,
                                TASKTAG_ARG1,       (Tag)*(struct ExecBase **)4,
                                TASKTAG_ARG2,       (Tag)mainPort,
                                TASKTAG_STACKSIZE,  65536,
                                TAG_DONE);
        (void)renderer;

        Printf("Waiting for welcome message form renderer...\n");

        WaitPort(mainPort);
        msg = GetMsg(mainPort);
        rendererPort = msg->mn_ReplyPort;
        ReplyMsg(msg);

        cmd.mm_Type = MSG_STARTUP;
        cmd.mm_Message.mn_Length = sizeof(cmd);
        cmd.mm_Message.mn_ReplyPort = mainPort;
        cmd.mm_Body.Startup.ChunkyBM = workBuffer;
        cmd.mm_Body.Startup.Width = width;
        cmd.mm_Body.Startup.Height = height;
        cmd.mm_Body.Startup.coreCount = max_cpus;
        cmd.mm_Body.Startup.numberOfSamples = max_iter;
        cmd.mm_Body.Startup.explicitMode = explicit_mode;

        Printf("... renderer alive. sending startup message\n");

        PutMsg(rendererPort, &cmd.mm_Message);
        WaitPort(mainPort);
        GetMsg(mainPort);

        Printf("Entering main loop\n");

        GetSysTime(&start_time);

        while ((!windowClosing) && ((signals = Wait(SIGBREAKF_CTRL_D | (1 << displayWin->UserPort->mp_SigBit) | (1 << mainPort->mp_SigBit))) != 0))
        {
            // CTRL_D is show time signal
            if (signals & SIGBREAKF_CTRL_D)
            {
                GetSysTime(&now);
                SubTime(&now, &start_time);

                Printf("Rendering time: %ld:%02ld:%02ld\n",
                    now.tv_secs / 3600, (now.tv_secs / 60) % 60, now.tv_secs % 60);
            }
            if (signals & (1 << displayWin->UserPort->mp_SigBit))
            {
                struct IntuiMessage *msg;
                while ((msg = (struct IntuiMessage *)GetMsg(displayWin->UserPort)))
                {
                    switch(msg->Class)
                    {
                        case IDCMP_CLOSEWINDOW:
                            windowClosing = TRUE;
                            break;

                        case IDCMP_REFRESHWINDOW:
                            BeginRefresh(msg->IDCMPWindow);
                            BltBitMapRastPort (outputBMap, 0, 0,
                                msg->IDCMPWindow->RPort, msg->IDCMPWindow->BorderLeft, msg->IDCMPWindow->BorderTop,
                                width, height, 0xC0);
                            EndRefresh(msg->IDCMPWindow, TRUE);
                            break;
                    }
                    ReplyMsg((struct Message *)msg);
                }
            }
            if (signals & (1 << mainPort->mp_SigBit))
            {
                struct MyMessage *msg;

                while ((msg = (struct MyMessage *)GetMsg(mainPort)))
                {
                    if (msg->mm_Message.mn_Length == sizeof(struct MyMessage))
                    {
                        switch (msg->mm_Type)
                        {
                            case MSG_REDRAWTILE:
                                p96WritePixelArray(&ri, 
                                            msg->mm_Body.RedrawTile.TileX * TILE_SIZE, 
                                            msg->mm_Body.RedrawTile.TileY * TILE_SIZE,
                                            outBMRastPort, 
                                            msg->mm_Body.RedrawTile.TileX * TILE_SIZE, 
                                            msg->mm_Body.RedrawTile.TileY * TILE_SIZE,
                                           TILE_SIZE, TILE_SIZE);
                                

                                BltBitMapRastPort (outputBMap, 
                                            msg->mm_Body.RedrawTile.TileX * TILE_SIZE, 
                                            msg->mm_Body.RedrawTile.TileY * TILE_SIZE,
                                            displayWin->RPort, 
                                            displayWin->BorderLeft + msg->mm_Body.RedrawTile.TileX * TILE_SIZE, 
                                            displayWin->BorderTop + msg->mm_Body.RedrawTile.TileY * TILE_SIZE,
                                            TILE_SIZE, TILE_SIZE, 0xC0);
                                break;
                            
                            case MSG_STATS:
                                tasksWork = msg->mm_Body.Stats.tasksWork;
                                tasksIn = msg->mm_Body.Stats.tasksIn;
                                tasksOut = msg->mm_Body.Stats.tasksOut;
                                
                                GetSysTime(&now);
                                SubTime(&now, &start_time);
                                _sprintf(tmpbuf, "Rendering (%d in work, %d waiting, %d done): %d:%02d:%02d",
                                    tasksWork, tasksIn, tasksOut,
                                    now.tv_secs / 3600,
                                    (now.tv_secs / 60) % 60,
                                    now.tv_secs % 60);
                                SetWindowTitles(displayWin, tmpbuf, NULL);
                                if ((busyPointer) && (msg->mm_Body.Stats.tasksWork == 0))
                                {
                                    SetWindowPointer(displayWin, WA_BusyPointer, FALSE, TAG_DONE);
                                    busyPointer = FALSE;
                                }
                                else if ((!busyPointer) && (msg->mm_Body.Stats.tasksWork > 0))
                                {
                                    SetWindowPointer(displayWin, WA_BusyPointer, TRUE, TAG_DONE);
                                    busyPointer = TRUE;
                                }
                                break;

                            default:
                                break;
                        }
                        ReplyMsg(&msg->mm_Message);
                    }
                }
            }
        }

        Printf("[SMP-Smallpt] Send DIE msg to renderer\n", __func__);

        struct MyMessage quitmsg;
        quitmsg.mm_Message.mn_ReplyPort = mainPort;
        quitmsg.mm_Message.mn_Length = sizeof(quitmsg);
        quitmsg.mm_Type = MSG_DIE;

        PutMsg(rendererPort, &quitmsg.mm_Message);
        int can_quit = 0;
        do {
            WaitPort(mainPort);
            struct MyMessage *msg;
            while ((msg = (struct MyMessage *)GetMsg(mainPort)))
                if (msg->mm_Type == MSG_DIE)
                    can_quit = 1;
        } while(!can_quit);

        DeleteMsgPort(mainPort);

        CloseWindow(displayWin);
        FreeMem(outBMRastPort, sizeof(struct RastPort));
        FreeBitMap(outputBMap);
        FreeMem(workBuffer, width * height * sizeof(ULONG));
    }

    FreeArgs(rda);

    return 0;
}