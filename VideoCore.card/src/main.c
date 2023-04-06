#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <exec/execbase.h>
#include <clib/debug_protos.h>
#include <devices/inputevent.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/input.h>
#include <proto/devicetree.h>

#include <stdint.h>

#include "boardinfo.h"
#include "emu68-vc4.h"
#include "mbox.h"
#include "support.h"
#include "vc4.h"
#include "vc6.h"

int __attribute__((no_reorder)) _start()
{
        return -1;
}

extern const char deviceEnd;
extern const char deviceName[];
extern const char deviceIdString[];
extern const uint32_t InitTable[];

const struct Resident RomTag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (APTR)&deviceEnd,
    RTF_AUTOINIT,
    VC4CARD_VERSION,
    NT_LIBRARY,
    VC4CARD_PRIORITY,
    (char *)((intptr_t)&deviceName),
    (char *)((intptr_t)&deviceIdString),
    (APTR)InitTable,
};

const char deviceName[] = CARD_NAME;
const char deviceIdString[] = VERSION_STRING;

/*
    Some properties, like e.g. #size-cells, are not always available in a key, but in that case the properties
    should be searched for in the parent. The process repeats recursively until either root key is found
    or the property is found, whichever occurs first
*/
CONST_APTR GetPropValueRecursive(APTR key, CONST_STRPTR property, APTR DeviceTreeBase)
{
    do {
        /* Find the property first */
        APTR prop = DT_FindProperty(key, property);

        if (prop)
        {
            /* If property is found, get its value and exit */
            return DT_GetPropValue(prop);
        }
        
        /* Property was not found, go to the parent and repeat */
        key = DT_GetParent(key);
    } while (key);

    return NULL;
}

int _strcmp(const char *s1, const char *s2)
{
    while (*s1 == *s2++)
        if (*s1++ == '\0')
            return (0);
    return (*(const unsigned char *)s1 - *(const unsigned char *)(s2 - 1));
}

static int FindCard(struct BoardInfo* bi asm("a0"), struct VC4Base *VC4Base asm("a6"))
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    APTR DeviceTreeBase = NULL;
    APTR key;

    // Cancel loading the driver if left or right shift is being held down.
    struct IORequest io;

    bug("[VC] FindCard\n");

    if (OpenDevice((STRPTR)"input.device", 0, &io, 0) == 0)
    {
        struct Library *InputBase = (struct Library *)io.io_Device;
        UWORD qual = PeekQualifier();
        CloseDevice(&io);

        if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
        {
            bug("[VC] Shift was pressed, ignoring VideoCore\n");
            return 0;
        }
            
    }

    /* Open device tree resource */
    DeviceTreeBase = (struct Library *)OpenResource((STRPTR)"devicetree.resource");
    if (DeviceTreeBase == 0) {
        // If devicetree.resource can't be opened, this probably isn't Emu68.
        return 0;
    }
    VC4Base->vc4_DeviceTreeBase = DeviceTreeBase;

    /* Open DOS, Expansion and Intuition, but I don't know yet why... */
    VC4Base->vc4_ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 0);
    
    if (VC4Base->vc4_ExpansionBase == NULL) {
        return 0;
    }

    VC4Base->vc4_IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
    
    if (VC4Base->vc4_IntuitionBase == NULL) {
        CloseLibrary((struct Library *)VC4Base->vc4_ExpansionBase);
        return 0;
    }

    VC4Base->vc4_DOSBase = (struct DOSBase *)OpenLibrary("dos.library", 0);

    if (VC4Base->vc4_DOSBase == NULL) {
        CloseLibrary((struct Library *)VC4Base->vc4_IntuitionBase);
        CloseLibrary((struct Library *)VC4Base->vc4_ExpansionBase);
        return 0;
    }

    /* Alloc 128-byte aligned memory for mailbox requests */
    VC4Base->vc4_RequestBase = AllocMem(MBOX_SIZE, MEMF_FAST);

    if (VC4Base->vc4_RequestBase == NULL) {
        CloseLibrary((struct Library *)VC4Base->vc4_DOSBase);
        CloseLibrary((struct Library *)VC4Base->vc4_IntuitionBase);
        CloseLibrary((struct Library *)VC4Base->vc4_ExpansionBase);
        return 0;
    }

    VC4Base->vc4_Request = (ULONG *)(((intptr_t)VC4Base->vc4_RequestBase + 127) & ~127);

    bug("[VC] Request buffer at %08lx\n", VC4Base->vc4_Request);

    /* Get VC4 physical address of mailbox interface. Subsequently it will be translated to m68k physical address */
    key = DT_OpenKey("/aliases");
    if (key)
    {
        CONST_STRPTR mbox_alias = DT_GetPropValue(DT_FindProperty(key, "mailbox"));

        DT_CloseKey(key);
        
        if (mbox_alias != NULL)
        {
            key = DT_OpenKey(mbox_alias);

            if (key)
            {
                int size_cells = 1;
                int address_cells = 1;

                const ULONG * siz = GetPropValueRecursive(key, "#size_cells", DeviceTreeBase);
                const ULONG * addr = GetPropValueRecursive(key, "#address-cells", DeviceTreeBase);

                if (siz != NULL)
                    size_cells = *siz;
                
                if (addr != NULL)
                    address_cells = *addr;

                const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "reg"));

                VC4Base->vc4_MailBox = (APTR)reg[address_cells - 1];

                DT_CloseKey(key);
            }
        }
    }

    /* Open /soc key and learn about VC4 to CPU mapping. Use it to adjust the addresses obtained above */
    key = DT_OpenKey("/soc");
    if (key)
    {
        int size_cells = 1;
        int address_cells = 1;
        int cpu_address_cells = 1;

        const ULONG * siz = GetPropValueRecursive(key, "#size_cells", DeviceTreeBase);
        const ULONG * addr = GetPropValueRecursive(key, "#address-cells", DeviceTreeBase);
        const ULONG * cpu_addr = DT_GetPropValue(DT_FindProperty(DT_OpenKey("/"), "#address-cells"));

        if (siz != NULL)
            size_cells = *siz;
        
        if (addr != NULL)
            address_cells = *addr;

        if (cpu_addr != NULL)
            cpu_address_cells = *cpu_addr;

        const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "ranges"));

        ULONG phys_vc4 = reg[address_cells - 1];
        ULONG phys_cpu = reg[address_cells + cpu_address_cells - 1];

        VC4Base->vc4_MailBox = (APTR)((ULONG)VC4Base->vc4_MailBox - phys_vc4 + phys_cpu);

        DT_CloseKey(key);
    }

    bug("[VC] MailBox at %08lx\n", VC4Base->vc4_MailBox);

    /* Find out base address of framebuffer and video memory size */
    get_vc_memory(&VC4Base->vc4_MemBase, &VC4Base->vc4_MemSize, VC4Base);

    bug("[VC] GPU memory at %08lx, size: %ld KB\n", VC4Base->vc4_MemBase, (ULONG)VC4Base->vc4_MemSize / 1024);

    /* Set basic data in BoardInfo structure */

    /* Get the block memory which was reserved by Emu68 on early startup. It has proper caching already */
    key = DT_OpenKey("/emu68");
    if (key)
    {
        const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "vc4-mem"));

        if (reg == NULL)
        {
            FreeMem(VC4Base->vc4_RequestBase, MBOX_SIZE);          
            CloseLibrary((struct Library *)VC4Base->vc4_DOSBase);
            CloseLibrary((struct Library *)VC4Base->vc4_IntuitionBase);
            CloseLibrary((struct Library *)VC4Base->vc4_ExpansionBase);
            
            return 0;
        }

        bi->MemoryBase = (APTR)reg[0];
        bi->MemorySize = reg[1];

        DT_CloseKey(key);
    }

    bi->RegisterBase = NULL;

    bug("[VC] Memory base at %08lx, size %ldMB\n", (ULONG)bi->MemoryBase, bi->MemorySize / (1024*1024));

    while (VC4Base->vc4_DispSize.width == 0 || VC4Base->vc4_DispSize.height == 0)
    {
        VC4Base->vc4_DispSize = get_display_size(VC4Base);
    }

    bug("[VC] Physical display size: %ld x %ld\n", (ULONG)VC4Base->vc4_DispSize.width, (ULONG)VC4Base->vc4_DispSize.height);

    VC4Base->vc4_ActivePlane = -1;

    VC4Base->vc4_VideoCore6 = 0;

    key = DT_OpenKey("/gpu");
    if (key)
    {
        const char *comp = DT_GetPropValue(DT_FindProperty(key, "compatible"));

        if (comp != NULL)
        {
            if (_strcmp("brcm,bcm2711-vc5", comp) == 0)
            {
                bug("[VC] VideoCore6 detected\n");
                VC4Base->vc4_VideoCore6 = 1;
            }
        }
    }

#if 0
    VC4Base->vc4_VPU_CopyBlock = (APTR)upload_code(vpu_block_copy, sizeof(vpu_block_copy), VC4Base);

    RawDoFmt("[vc4] VPU CopyBlock pointer at %08lx\n", &VC4Base->vc4_VPU_CopyBlock, (APTR)putch, NULL);
#endif

    return 1;
}

#include "messages.h"

static void vc4_Task()
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    struct Task *me = FindTask(NULL);
    struct VC4Base *VC4Base = me->tc_UserData;
    struct MsgPort *port = CreateMsgPort();
    ULONG sigset;
    volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);

    port->mp_Node.ln_Name = "VideoCore";
    AddPort(port);

    VC4Base->vc4_Port = port;
    
    do {
        sigset = Wait(SIGBREAKF_CTRL_C | (1 << port->mp_SigBit));
        if (sigset & (1 << port->mp_SigBit))
        {
            struct Message *msg;
            
            while((msg = GetMsg(port)))
            {
                if (msg->mn_Length == sizeof(struct VC4Msg)) {
                    struct VC4Msg *vmsg = (struct VC4Msg *)msg;

                    switch (vmsg->cmd) {
                        case VCMD_SET_KERNEL:
                            kernel_start ^= 0x10;
                            if (vmsg->SetKernel.kernel) {
                                if (VC4Base->vc4_VideoCore6)
                                    compute_scaling_kernel((uint32_t *)0xf2404000, vmsg->SetKernel.b, vmsg->SetKernel.c);
                                else
                                    compute_scaling_kernel((uint32_t *)0xf2402000, vmsg->SetKernel.b, vmsg->SetKernel.c);
                            }
                            else {
                                if (VC4Base->vc4_VideoCore6)
                                    compute_nearest_neighbour_kernel((uint32_t *)0xf2404000);
                                else
                                    compute_nearest_neighbour_kernel((uint32_t *)0xf2402000);
                            }
                            if (VC4Base->vc4_Kernel)
                            {
                                // Wait for vertical blank before updating the display list
                                do { asm volatile("nop"); } while((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height);

                                VC4Base->vc4_Kernel[0] = LE32(kernel_start);
                                VC4Base->vc4_Kernel[1] = LE32(kernel_start);
                                VC4Base->vc4_Kernel[2] = LE32(kernel_start);
                                VC4Base->vc4_Kernel[3] = LE32(kernel_start);                               

                                VC4Base->vc4_MouseCoord[12] = LE32(kernel_start);
                                VC4Base->vc4_MouseCoord[13] = LE32(kernel_start);
                                VC4Base->vc4_MouseCoord[14] = LE32(kernel_start);
                                VC4Base->vc4_MouseCoord[15] = LE32(kernel_start);
                            }
                            break;
                        
                        case VCMD_GET_KERNEL:
                            vmsg->GetKernel.kernel = VC4Base->vc4_UseKernel;
                            vmsg->GetKernel.b = VC4Base->vc4_Kernel_B;
                            vmsg->GetKernel.c = VC4Base->vc4_Kernel_C;
                            break;
                        
                        case VCMD_GET_SCALER:
                            if (VC4Base->vc4_PlaneScalerX) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerX);
                                vmsg->GetScaler.val = (val >> 30) & 3;
                            }
                            else
                                vmsg->GetScaler.val = 0;
                            break;

                        case VCMD_SET_SCALER:
                            // Wait for vertical blank before updating the display list
                            do { asm volatile("nop"); } while((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height);

                            if (VC4Base->vc4_PlaneScalerX) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerX);
                                val = (val & 0x3fffffff) | (vmsg->SetScaler.val << 30);
                                *VC4Base->vc4_PlaneScalerX = LE32(val);
                            }
                            if (VC4Base->vc4_PlaneScalerY) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerY);
                                val = (val & 0x3fffffff) | (vmsg->SetScaler.val << 30);
                                *VC4Base->vc4_PlaneScalerY = LE32(val);
                            }

                            if (VC4Base->vc4_ScaleX != 0x10000) {
                                ULONG val = LE32(VC4Base->vc4_MouseCoord[9]);
                                val = (val & 0x3fffffff) | (vmsg->SetScaler.val << 30);
                                VC4Base->vc4_MouseCoord[9] = LE32(val);

                                val = LE32(VC4Base->vc4_MouseCoord[10]);
                                val = (val & 0x3fffffff) | (vmsg->SetScaler.val << 30);
                                VC4Base->vc4_MouseCoord[10] = LE32(val);
                            }
                            break;
                        
                        case VCMD_GET_PHASE:
                            if (VC4Base->vc4_PlaneScalerX) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerX);
                                vmsg->GetPhase.val = val & 0xff;
                            }
                            else
                                vmsg->GetPhase.val = 0;
                            break;

                        case VCMD_SET_PHASE:
                            // Wait for vertical blank before updating the display list
                            do { asm volatile("nop"); } while((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height);

                            if (VC4Base->vc4_PlaneScalerX) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerX);
                                val = (val & 0xffffff00) | (vmsg->SetPhase.val & 0xff);
                                *VC4Base->vc4_PlaneScalerX = LE32(val);
                            }
                            if (VC4Base->vc4_PlaneScalerY) {
                                ULONG val = LE32(*VC4Base->vc4_PlaneScalerY);
                                val = (val & 0xffffff00) | (vmsg->SetPhase.val & 0xff);
                                *VC4Base->vc4_PlaneScalerY = LE32(val);
                            }

                            if (VC4Base->vc4_ScaleX != 0x10000) {
                                ULONG val = LE32(VC4Base->vc4_MouseCoord[9]);
                                val = (val & 0xffffff00) | (vmsg->SetPhase.val & 0xff);
                                VC4Base->vc4_MouseCoord[9] = LE32(val);

                                val = LE32(VC4Base->vc4_MouseCoord[10]);
                                val = (val & 0xffffff00) | (vmsg->SetPhase.val & 0xff);
                                VC4Base->vc4_MouseCoord[10] = LE32(val);
                            }
                            break;
                    }
                }
                ReplyMsg(msg);
            }
        }
    } while ((sigset & SIGBREAKF_CTRL_C) == 0);

    RemPort(port);
    DeleteMsgPort(port);
}

static int InitCard(struct BoardInfo* bi asm("a0"), const char **ToolTypes asm("a1"), struct VC4Base *VC4Base asm("a6"))
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    bi->CardBase = (struct CardBase *)VC4Base;
    bi->ExecBase = VC4Base->vc4_SysBase;
    bi->BoardName = "VideoCore";
    bi->BoardType = 14;
    bi->PaletteChipType = PCT_S3ViRGE;
    bi->GraphicsControllerType = GCT_S3ViRGE;

    bi->Flags |= BIF_GRANTDIRECTACCESS | BIF_FLICKERFIXER | BIF_HARDWARESPRITE | BIF_BLITTER;
    bi->RGBFormats = 
        RGBFF_TRUEALPHA | 
        RGBFF_TRUECOLOR | 
        RGBFF_R5G6B5PC | RGBFF_R5G5B5PC | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC | // RGBFF_HICOLOR | 
        RGBFF_CLUT; //RGBFF_HICOLOR | RGBFF_TRUEALPHA | RGBFF_CLUT;
    bi->SoftSpriteFlags = 0;
    bi->BitsPerCannon = 8;

    for(int i = 0; i < MAXMODES; i++) {
        bi->MaxHorValue[i] = 8192;
        bi->MaxVerValue[i] = 8192;
        bi->MaxHorResolution[i] = 8192;
        bi->MaxVerResolution[i] = 8192;
        bi->PixelClockCount[i] = 1;
    }

    bi->MemoryClock = CLOCK_HZ;

    if (VC4Base->vc4_VideoCore6)
    {
// Basic P96 functions needed for "dumb frame buffer" operation
        bi->SetSwitch = (void *)VC6_SetSwitch;
        bi->SetColorArray = (void *)VC6_SetColorArray;
        bi->SetDAC = (void *)VC6_SetDAC;
        bi->SetGC = (void *)VC6_SetGC;
        bi->SetPanning = (void *)VC6_SetPanning;
        bi->CalculateBytesPerRow = (void *)VC6_CalculateBytesPerRow;
        bi->CalculateMemory = (void *)VC6_CalculateMemory;
        bi->GetCompatibleFormats = (void *)VC6_GetCompatibleFormats;
        bi->SetDisplay = (void *)VC6_SetDisplay;

        bi->ResolvePixelClock = (void *)VC6_ResolvePixelClock;
        bi->GetPixelClock = (void *)VC6_GetPixelClock;
        bi->SetClock = (void *)VC6_SetClock;

        bi->SetMemoryMode = (void *)VC6_SetMemoryMode;
        bi->SetWriteMask = (void *)VC6_SetWriteMask;
        bi->SetClearMask = (void *)VC6_SetClearMask;
        bi->SetReadPlane = (void *)VC6_SetReadPlane;

        bi->WaitVerticalSync = (void *)VC6_WaitVerticalSync;

        // Additional functions for "blitter" acceleration and vblank handling
        //bi->SetInterrupt = (void *)NULL;

        bi->WaitBlitter = (void *)VC6_WaitBlitter;

        //bi->ScrollPlanar = (void *)NULL;
        //bi->UpdatePlanar = (void *)NULL;

        bi->BlitPlanar2Chunky = (void *)VC6_BlitPlanar2Chunky;
        bi->BlitPlanar2Direct = (void *)VC6_BlitPlanar2Direct;

        bi->FillRect = (void *)VC6_FillRect;
        bi->InvertRect = (void *)VC6_InvertRect;
        bi->BlitRect = (void *)VC6_BlitRect;
        bi->BlitTemplate = (void *)VC6_BlitTemplate;
        bi->BlitPattern = (void *)VC6_BlitPattern;
        bi->DrawLine = (void *)VC6_DrawLine;
        bi->BlitRectNoMaskComplete = (void *)VC6_BlitRectNoMaskComplete;
        //bi->EnableSoftSprite = (void *)NULL;

        //bi->AllocCardMemAbs = (void *)NULL;
        //bi->SetSplitPosition = (void *)NULL;
        //bi->ReInitMemory = (void *)NULL;
        //bi->WriteYUVRect = (void *)NULL;
        bi->GetVSyncState = (void *)VC6_GetVSyncState;
        bi->GetVBeamPos = (void *)VC6_GetVBeamPos;
        //bi->SetDPMSLevel = (void *)NULL;
        //bi->ResetChip = (void *)NULL;
        //bi->GetFeatureAttrs = (void *)NULL;
        //bi->AllocBitMap = (void *)NULL;
        //bi->FreeBitMap = (void *)NULL;
        //bi->GetBitMapAttr = (void *)NULL;

        bi->SetSprite = (void *)VC6_SetSprite;
        bi->SetSpritePosition = (void *)VC6_SetSpritePosition;
        bi->SetSpriteImage = (void *)VC6_SetSpriteImage;
        bi->SetSpriteColor = (void *)VC6_SetSpriteColor;

        //bi->CreateFeature = (void *)NULL;
        //bi->SetFeatureAttrs = (void *)NULL;
        //bi->DeleteFeature = (void *)NULL;
    }
    else
    {
        // Basic P96 functions needed for "dumb frame buffer" operation
        bi->SetSwitch = (void *)SetSwitch;
        bi->SetColorArray = (void *)SetColorArray;
        bi->SetDAC = (void *)SetDAC;
        bi->SetGC = (void *)SetGC;
        bi->SetPanning = (void *)SetPanning;
        bi->CalculateBytesPerRow = (void *)CalculateBytesPerRow;
        bi->CalculateMemory = (void *)CalculateMemory;
        bi->GetCompatibleFormats = (void *)GetCompatibleFormats;
        bi->SetDisplay = (void *)SetDisplay;

        bi->ResolvePixelClock = (void *)ResolvePixelClock;
        bi->GetPixelClock = (void *)GetPixelClock;
        bi->SetClock = (void *)SetClock;

        bi->SetMemoryMode = (void *)SetMemoryMode;
        bi->SetWriteMask = (void *)SetWriteMask;
        bi->SetClearMask = (void *)SetClearMask;
        bi->SetReadPlane = (void *)SetReadPlane;

        bi->WaitVerticalSync = (void *)WaitVerticalSync;

        // Additional functions for "blitter" acceleration and vblank handling
        //bi->SetInterrupt = (void *)NULL;

        //bi->WaitBlitter = (void *)NULL;

        //bi->ScrollPlanar = (void *)NULL;
        //bi->UpdatePlanar = (void *)NULL;

        //bi->BlitPlanar2Chunky = (void *)BlitPlanar2Chunky;
        //bi->BlitPlanar2Direct = (void *)BlitPlanar2Direct;

        //bi->FillRect = (void *)FillRect;
        //bi->InvertRect = (void *)InvertRect;
        //bi->BlitRect = (void *)BlitRect;
        //bi->BlitTemplate = (void *)BlitTemplate;
        //bi->BlitPattern = (void *)BlitPattern;
        //bi->DrawLine = (void *)DrawLine;
        //bi->BlitRectNoMaskComplete = (void *)BlitRectNoMaskComplete;
        //bi->EnableSoftSprite = (void *)NULL;

        //bi->AllocCardMemAbs = (void *)NULL;
        //bi->SetSplitPosition = (void *)NULL;
        //bi->ReInitMemory = (void *)NULL;
        //bi->WriteYUVRect = (void *)NULL;
        bi->GetVSyncState = (void *)GetVSyncState;
        bi->GetVBeamPos = (void *)GetVBeamPos;
        //bi->SetDPMSLevel = (void *)NULL;
        //bi->ResetChip = (void *)NULL;
        //bi->GetFeatureAttrs = (void *)NULL;
        //bi->AllocBitMap = (void *)NULL;
        //bi->FreeBitMap = (void *)NULL;
        //bi->GetBitMapAttr = (void *)NULL;

        bi->SetSprite = (void *)SetSprite;
        bi->SetSpritePosition = (void *)SetSpritePosition;
        bi->SetSpriteImage = (void *)SetSpriteImage;
        bi->SetSpriteColor = (void *)SetSpriteColor;

        //bi->CreateFeature = (void *)NULL;
        //bi->SetFeatureAttrs = (void *)NULL;
        //bi->DeleteFeature = (void *)NULL;
    }

    
    bug("[VC] Measuring refresh rate\n");

    Disable();

    volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);

    ULONG cnt1 = *stat & LE32(0x3f << 12);
    ULONG cnt2;

    /* Wait for the very next frame */
    do { cnt2 = *stat & LE32(0x3f << 12); } while(cnt2 == cnt1);
    
    /* Get current tick number */
    ULONG tick1 = LE32(*(volatile uint32_t*)0xf2003004);

    /* Wait for the very next frame */
    do { cnt1 = *stat & LE32(0x3f << 12); } while(cnt2 == cnt1);

    /* Get current tick number */
    ULONG tick2 = LE32(*(volatile uint32_t*)0xf2003004);

    Enable();

    double delta = (double)(tick2 - tick1);
    double hz = 1000000.0 / delta;

    ULONG mHz = 1000.0 * hz;

    bug("[VC] Detected refresh rate of %ld.%03ld Hz\n", mHz / 1000, mHz % 1000);

    VC4Base->vc4_VertFreq = (ULONG)(hz+0.5);

    VC4Base->vc4_Phase = 128;
    VC4Base->vc4_Scaler = 0xc0000000;
    VC4Base->vc4_UseKernel = 1;
    VC4Base->vc4_SpriteAlpha = 255;

    for (;ToolTypes[0] != NULL; ToolTypes++)
    {
        const char *tt = ToolTypes[0];

        bug("[VC] Checking ToolType `%s`\n", tt);

        if (_strcmp(tt, "VC4_PHASE") == '=')
        {
            const char *c = &tt[10];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            VC4Base->vc4_Phase = num;
            bug("[VC] Setting VC4 phase to %ld\n", num);
        }
        else if (_strcmp(tt, "VC4_VERT") == '=')
        {
            const char *c = &tt[10];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            VC4Base->vc4_VertFreq = num;
            bug("[VC] Setting vertical frequency to %ld\n", num);
        }
        else if (_strcmp(tt, "VC4_SCALER") == '=')
        {
            switch(tt[11]) {
                case '0':
                    VC4Base->vc4_Scaler = 0x00000000;
                    break;
                case '1':
                    VC4Base->vc4_Scaler = 0x40000000;
                    break;
                case '2':
                    VC4Base->vc4_Scaler = 0x80000000;
                    break;
                case '3':
                    VC4Base->vc4_Scaler = 0xc0000000;
                    break;
            }

            bug("[VC] Setting VC4 scaler to %lx\n", VC4Base->vc4_Scaler);
        }
        else if (_strcmp(tt, "VC4_KERNEL") == '=')
        {
            const char *c = &tt[11];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            if (num == 0)
                VC4Base->vc4_UseKernel = 0;
            else
                VC4Base->vc4_UseKernel = 1;
        }
        else if (_strcmp(tt, "VC4_KERNEL_B") == '=')
        {
            const char *c = &tt[13];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            VC4Base->vc4_Kernel_B = (double)num / 1000.0;

            bug("[VC] Mitchel-Netravali B %ld\n", num);
        }
        else if (_strcmp(tt, "VC4_SPRITE_OPACITY") == '=')
        {
            const char *c = &tt[19];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            if (num > 255) num=255;

            VC4Base->vc4_SpriteAlpha = num;
            bug("[VC] Sprite opacity set to %ld\n", num);
        }
        else if (_strcmp(tt, "VC4_KERNEL_C") == '=')
        {
            const char *c = &tt[13];
            ULONG num = 0;

            while (*c) {
                if (*c < '0' || *c > '9')
                    break;
                
                num = num * 10 + (*c++ - '0');
            }

            VC4Base->vc4_Kernel_C = (double)num / 1000.0;

            bug("[VC] Mitchel-Netravali C %ld\n", num);
        }
    }

    if (VC4Base->vc4_UseKernel)
        if (VC4Base->vc4_VideoCore6)
            compute_scaling_kernel((uint32_t *)0xf2404000, VC4Base->vc4_Kernel_B, VC4Base->vc4_Kernel_C);
        else
            compute_scaling_kernel((uint32_t *)0xf2402000, VC4Base->vc4_Kernel_B, VC4Base->vc4_Kernel_C);
    else
        if (VC4Base->vc4_VideoCore6)
            compute_nearest_neighbour_kernel((uint32_t *)0xf2404000);
        else
            compute_nearest_neighbour_kernel((uint32_t *)0xf2402000);

    if (VC4Base->vc4_VideoCore6)
        compute_nearest_neighbour_kernel(((uint32_t *)0xf2404000) - kernel_start + unity_kernel);
    else
        compute_nearest_neighbour_kernel(((uint32_t *)0xf2402000) - kernel_start + unity_kernel);

    VC4Base->vc4_Task = NewCreateTask(
        TASKTAG_PC,         (Tag)vc4_Task,
        TASKTAG_NAME,       (Tag)"VideoCore Task",
        TASKTAG_STACKSIZE,  10240,
        TASKTAG_USERDATA,   (Tag)VC4Base,
        TAG_DONE
    );

    VC4Base->vc4_SpriteShape = AllocMem(MAXSPRITEWIDTH * MAXSPRITEHEIGHT, MEMF_FAST | MEMF_REVERSE | MEMF_CLEAR);

    bug("[VC] InitCard ready\n");

    return 1;
}

static struct VC4Base * OpenLib(ULONG version asm("d0"), struct VC4Base *VC4Base asm("a6"))
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    VC4Base->vc4_LibNode.LibBase.lib_OpenCnt++;
    VC4Base->vc4_LibNode.LibBase.lib_Flags &= ~LIBF_DELEXP;

    bug("[VC] OpenLib\n");

    return VC4Base;
}

static ULONG ExpungeLib(struct VC4Base *VC4Base asm("a6"))
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    BPTR segList = 0;

    if (VC4Base->vc4_LibNode.LibBase.lib_OpenCnt == 0)
    {
        /* Free memory of mailbox request buffer */
        FreeMem(VC4Base->vc4_RequestBase, 4*256);

        /* Remove library from Exec's list */
        Remove(&VC4Base->vc4_LibNode.LibBase.lib_Node);

        /* Close all eventually opened libraries */
        if (VC4Base->vc4_ExpansionBase != NULL)
            CloseLibrary((struct Library *)VC4Base->vc4_ExpansionBase);
        if (VC4Base->vc4_DOSBase != NULL)
            CloseLibrary((struct Library *)VC4Base->vc4_DOSBase);
        if (VC4Base->vc4_IntuitionBase != NULL)
            CloseLibrary((struct Library *)VC4Base->vc4_IntuitionBase);

        /* Save seglist */
        segList = VC4Base->vc4_SegList;

        /* Remove VC4Base itself - free the memory */
        ULONG size = VC4Base->vc4_LibNode.LibBase.lib_NegSize + VC4Base->vc4_LibNode.LibBase.lib_PosSize;
        FreeMem((APTR)((ULONG)VC4Base - VC4Base->vc4_LibNode.LibBase.lib_NegSize), size);
    }
    else
    {
        /* Library is still in use, set delayed expunge flag */
        VC4Base->vc4_LibNode.LibBase.lib_Flags |= LIBF_DELEXP;
    }

    /* Return 0 or segList */
    return segList;
}

static ULONG CloseLib(struct VC4Base *VC4Base asm("a6"))
{
    if (VC4Base->vc4_LibNode.LibBase.lib_OpenCnt != 0)
        VC4Base->vc4_LibNode.LibBase.lib_OpenCnt--;
    
    if (VC4Base->vc4_LibNode.LibBase.lib_OpenCnt == 0)
    {
        if (VC4Base->vc4_LibNode.LibBase.lib_Flags & LIBF_DELEXP)
            return ExpungeLib(VC4Base);
    }

    return 0;
}


static uint32_t ExtFunc()
{
    return 0;
}

struct VC4Base * vc4_Init(struct VC4Base *base asm("d0"), BPTR seglist asm("a0"), struct ExecBase *SysBase asm("a6"))
{
    struct VC4Base *VC4Base = base;
    VC4Base->vc4_SegList = seglist;
    VC4Base->vc4_SysBase = SysBase;
    VC4Base->vc4_LibNode.LibBase.lib_Revision = VC4CARD_REVISION;
    VC4Base->vc4_Enabled = -1;

    return VC4Base;
}

static uint32_t vc4_functions[] = {
    (uint32_t)OpenLib,
    (uint32_t)CloseLib,
    (uint32_t)ExpungeLib,
    (uint32_t)ExtFunc,
    (uint32_t)FindCard,
    (uint32_t)InitCard,
    -1
};

const uint32_t InitTable[4] = {
    sizeof(struct VC4Base), 
    (uint32_t)vc4_functions, 
    0, 
    (uint32_t)vc4_Init
};




#if 0

unsigned char a[] = {
  0x10, 0xf0, 0x00, 0xc0, 0x80, 0x03, 0x10, 0xf8, 0x40, 0xc0, 0x40, 0x00,
  0xc0, 0xf3, 0x00, 0x00, 0x10, 0xf8, 0x80, 0xc0, 0x00, 0x00, 0xc0, 0xf3,
  0x01, 0x00, 0x10, 0xf8, 0xc0, 0xc0, 0x40, 0x00, 0xc0, 0xf3, 0x01, 0x00,
  0x90, 0xf0, 0x30, 0x00, 0x81, 0x03, 0x90, 0xf8, 0x30, 0x00, 0x40, 0x10,
  0xc0, 0xf3, 0x04, 0x00, 0x90, 0xf8, 0x30, 0x00, 0x00, 0x20, 0xc0, 0xf3,
  0x05, 0x00, 0x90, 0xf8, 0x30, 0x00, 0x40, 0x30, 0xc0, 0xf3, 0x05, 0x00,
  0x40, 0xe8, 0x00, 0x01, 0x00, 0x00, 0x41, 0xe8, 0x00, 0x01, 0x00, 0x00,
  0x12, 0x66, 0x02, 0x6a, 0xd4, 0x18, 0x5a, 0x00
};

void test()
{
    int c = 1;
    FBReq[c++] = 0;
    FBReq[c++] = LE32(0x3000c);
    FBReq[c++] = LE32(12);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(sizeof(a));  // 32 bytes
    FBReq[c++] = LE32(4);   // 4 byte align
    FBReq[c++] = LE32((3 << 2) | (1 << 6));   // COHERENT | DIRECT | HINT_PERMALOCK
    FBReq[c++] = 0;
    FBReq[0] = LE32(4*c);

    arm_flush_cache((intptr_t)FBReq, 4*c);
    mbox_send(8, mmu_virt2phys((intptr_t)FBReq));
    mbox_recv(8);

    int handle = LE32(FBReq[5]);
    kprintf("Alloc returned %d\n", handle);

    c = 1;
    FBReq[c++] = 0;
    FBReq[c++] = LE32(0x0003000d);
    FBReq[c++] = LE32(4);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(handle);  // 32 bytes
    FBReq[c++] = 0;
    FBReq[0] = LE32(4*c);
    
    arm_flush_cache((intptr_t)FBReq, 4*c);
    mbox_send(8, mmu_virt2phys((intptr_t)FBReq));
    mbox_recv(8);

    uint64_t phys = LE32(FBReq[5]);
    uint64_t cpu = phys & ~0xc0000000;
    kprintf("Locl memory returned %08x, CPU addr %08x\n", phys, cpu);

    phys &= ~0xc0000000;
    for (unsigned i=0; i < sizeof(a); i++)
        ((uint8_t *)cpu)[i] = a[i];
    arm_flush_cache(cpu, sizeof(a));

    kprintf("test code uploaded\n");

    kprintf("running code with r0=%08x, r1=%08x, r2=%08x\n", 0xc0000000, fb_phys_base, 30000);

    uint32_t t0 = LE32(*(volatile uint32_t*)0xf2003004);

for (int i=0; i < 20000; i++) {
    if (i == 1)
        phys++;
    c = 1;
    FBReq[c++] = 0;
    FBReq[c++] = LE32(0x00030010);
    FBReq[c++] = LE32(28);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(phys);  // code address
    FBReq[c++] = LE32(0xc0000000 + (0x3fffffff & ((uint64_t)i * 15000*256))); // r0 source address
    FBReq[c++] = LE32(fb_phys_base); // r1 dest address
    FBReq[c++] = LE32(7500); // r2 Number of 256-byte packets
    FBReq[c++] = 0; // r3
    FBReq[c++] = 0; // r4
    FBReq[c++] = 0; // r5

    FBReq[c++] = 0;
    FBReq[0] = LE32(4*c);
    
    arm_flush_cache((intptr_t)FBReq, 4*c);
    mbox_send(8, mmu_virt2phys((intptr_t)FBReq));
    mbox_recv(8);
}

    uint32_t t1 = LE32(*(volatile uint32_t*)0xf2003004);

    kprintf("Returned from test code. Retval = %08x\n", LE32(FBReq[5]));
    kprintf("Time wasted: %d milliseconds\n", (t1 - t0) / 1000);
    
}

#endif
