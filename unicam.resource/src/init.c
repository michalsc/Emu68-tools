/*
    Copyright © 2023-2024 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/execbase.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/devicetree.h>
#include <common/compiler.h>

#include <inline/unicam.h>

#include "unicam.h"
#include "smoothing.h"
#include "mbox.h"
#include "videocore.h"

extern const char deviceName[];
extern const char deviceIdString[];

CONST_APTR GetPropValueRecursive(APTR key, CONST_STRPTR property, APTR DeviceTreeBase);
CONST_STRPTR FindToken(CONST_STRPTR string, CONST_STRPTR token);
int _strcmp(const char *s1, const char *s2);

APTR Init(REGARG(struct ExecBase *SysBase, "a6"))
{
    struct DeviceTreeBase *DeviceTreeBase = NULL;
    struct ExpansionBase *ExpansionBase = NULL;
    struct UnicamBase *UnicamBase = NULL;
    struct CurrentBinding binding;

    bug("[unicam] Init\n");

    DeviceTreeBase = OpenResource("devicetree.resource");

    if (DeviceTreeBase != NULL)
    {
        APTR base_pointer = NULL;
    
        ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 0);
        GetCurrentBinding(&binding, sizeof(binding));

        base_pointer = AllocMem(BASE_NEG_SIZE + BASE_POS_SIZE, MEMF_PUBLIC | MEMF_CLEAR);

        if (base_pointer != NULL)
        {
            BYTE start_on_boot = 0;
            APTR key;
            ULONG relFuncTable[UNICAM_FUNC_COUNT + 1];

            relFuncTable[0] = (ULONG)&L_UnicamStart;
            relFuncTable[1] = (ULONG)&L_UnicamStop;
            relFuncTable[2] = (ULONG)&L_UnicamGetFramebuffer;
            relFuncTable[3] = (ULONG)&L_UnicamGetFramebufferSize;
            relFuncTable[4] = (ULONG)&L_UnicamGetCropSize;
            relFuncTable[5] = (ULONG)&L_UnicamGetCropOffset;
            relFuncTable[6] = (ULONG)&L_UnicamGetKernel;
            relFuncTable[7] = (ULONG)&L_UnicamGetConfig;
            relFuncTable[8] = (ULONG)&L_UnicamGetSize;
            relFuncTable[9] = (ULONG)&L_UnicamGetMode;
            relFuncTable[10] = (ULONG)-1;

            UnicamBase = (struct UnicamBase *)((UBYTE *)base_pointer + BASE_NEG_SIZE);
            UnicamBase->u_SysBase = SysBase;

            MakeFunctions(UnicamBase, relFuncTable, 0);

            UnicamBase->u_ConfigDev = binding.cb_ConfigDev;
            UnicamBase->u_ROMBase = binding.cb_ConfigDev->cd_BoardAddr;
            UnicamBase->u_Node.lib_Node.ln_Type = NT_RESOURCE;
            UnicamBase->u_Node.lib_Node.ln_Pri = UNICAM_PRIORITY;
            UnicamBase->u_Node.lib_Node.ln_Name = (STRPTR)deviceName;
            UnicamBase->u_Node.lib_NegSize = BASE_NEG_SIZE;
            UnicamBase->u_Node.lib_PosSize = BASE_POS_SIZE;
            UnicamBase->u_Node.lib_Version = UNICAM_VERSION;
            UnicamBase->u_Node.lib_Revision = UNICAM_REVISION;
            UnicamBase->u_Node.lib_IdString = (STRPTR)deviceIdString;

            UnicamBase->u_RequestBase = AllocMem(256*4, MEMF_FAST);
            UnicamBase->u_Request = (ULONG *)(((intptr_t)UnicamBase->u_RequestBase + 127) & ~127);

            UnicamBase->u_IsVC6 = 0;
            UnicamBase->u_Offset.x = 0;
            UnicamBase->u_Offset.y = 0;
            UnicamBase->u_FullSize.width = UNICAM_WIDTH;
            UnicamBase->u_FullSize.height = UNICAM_HEIGHT;
            UnicamBase->u_Mode = UNICAM_MODE;
            UnicamBase->u_BPP = UNICAM_BPP;
            UnicamBase->u_Size = UnicamBase->u_FullSize;
            UnicamBase->u_Phase = 64;
            UnicamBase->u_Scaler = 3;
            UnicamBase->u_Smooth = 0;
            UnicamBase->u_Integer = 0;
            UnicamBase->u_KernelB = 250;
            UnicamBase->u_KernelC = 750;
            UnicamBase->u_Aspect = 1000;

            SumLibrary((struct Library*)UnicamBase);

            const char *cmdline = DT_GetPropValue(DT_FindProperty(DT_OpenKey("/chosen"), "bootargs"));
            const char *cmd;

            if (FindToken(cmdline, "unicam.boot"))
            {
                start_on_boot = 1;
                UnicamBase->u_StartOnBoot = TRUE;
                bug("[unicam] Starting HDMI passthrough on boot\n");
            }

            if (FindToken(cmdline, "unicam.integer"))
            {
                UnicamBase->u_Integer = 1;
                bug("[unicam] Use integer scaling\n");
            }

            if ((cmd = FindToken(cmdline, "unicam.b=")))
            {
                ULONG val = 0;

                for (int i=0; i < 4; i++)
                {
                    if (cmd[9 + i] < '0' || cmd[9 + i] > '9')
                        break;

                    val = val * 10 + cmd[9 + i] - '0';
                }

                if (val > 1000)
                    val = 1000;
                
                UnicamBase->u_KernelB = val;   
            }

            if ((cmd = FindToken(cmdline, "unicam.c=")))
            {
                ULONG val = 0;

                for (int i=0; i < 4; i++)
                {
                    if (cmd[9 + i] < '0' || cmd[9 + i] > '9')
                        break;

                    val = val * 10 + cmd[9 + i] - '0';
                }

                if (val > 1000)
                    val = 1000;
                
                UnicamBase->u_KernelC = val;
            }

            if ((cmd = FindToken(cmdline, "unicam.aspect=")))
            {
                ULONG val = 0;

                for (int i=0; i < 5; i++)
                {
                    if (cmd[14 + i] < '0' || cmd[14 + i] > '9')
                        break;

                    val = val * 10 + cmd[14 + i] - '0';
                }

                if (val > 3000)
                    val = 3000;
                if (val < 333)
                    val = 333;

                UnicamBase->u_Aspect = val;
            }

            if (FindToken(cmdline, "unicam.smooth"))
            {
                UnicamBase->u_Smooth = 1;
                bug("[unicam] Enable smoothing kernel. B=%ld, C=%ld\n", UnicamBase->u_KernelB, UnicamBase->u_KernelC);
            }

            if ((cmd = FindToken(cmdline, "unicam.scaler=")))
            {
                UnicamBase->u_Scaler = (cmd[14] - '0') & 3;
            }

            if ((cmd = FindToken(cmdline, "unicam.phase=")))
            {
                ULONG val = 0;

                for (int i=0; i < 3; i++)
                {
                    if (cmd[13 + i] < '0' || cmd[13 + i] > '9')
                        break;

                    val = val * 10 + cmd[13 + i] - '0';
                }

                if (val > 255)
                    val = 255;
                
                UnicamBase->u_Phase = val;
            }

            bug("[unicam] Scaler=%ld, Phase=%ld\n", UnicamBase->u_Scaler, UnicamBase->u_Phase);

            if ((cmd = FindToken(cmdline, "unicam.mode=")))
            {
                ULONG w = 0, h = 0, m = 0, bpp = 0;
                const char *c = &cmd[12];

                for (int i = 0; i < 4; i++)
                {
                    if (*c < '0' || *c > '9')
                    {
                        break;
                    }
                    w = w * 10 + *c++ - '0';
                }

                if (w != 0 && *c++ == ',')
                {
                    for (int i = 0; i < 4; i++)
                    {
                        if (*c < '0' || *c > '9')
                        {
                            break;
                        }
                        h = h * 10 + *c++ - '0';
                    }
                }

                if (*c++ == ',')
                {
                    for (int i = 0; i < 4; i++)
                    {
                        if (*c < '0' || *c > '9')
                        {
                            break;
                        }
                        m = m * 10 + *c++ - '0';
                    }
                }

                if (*c++ == ',')
                {
                    for (int i = 0; i < 4; i++)
                    {
                        if (*c < '0' || *c > '9')
                        {
                            break;
                        }
                        bpp = bpp * 10 + *c++ - '0';
                    }
                }

                if (w != 0 && h != 0)
                {
                    UnicamBase->u_FullSize.width = w;
                    UnicamBase->u_FullSize.height = h;
                    bug("[unicam] Overriding FullSize to %ld x %ld\n", UnicamBase->u_FullSize.width, UnicamBase->u_FullSize.height);
                }
                if (m != 0)
                {
                    UnicamBase->u_Mode = m;
                    bug("[unicam] Overriding mode to %lx\n", UnicamBase->u_Mode);
                }
                if (bpp != 0)
                {
                    UnicamBase->u_BPP = bpp;
                    bug("[unicam] Overriding bpp to %ld\n", UnicamBase->u_BPP);
                }
            }

            if ((cmd = FindToken(cmdline, "unicam.size=")))
            {
                ULONG x = 0, y = 0;
                const char *c = &cmd[12];

                for (int i=0; i < 4; i++)
                {
                    if (*c < '0' || *c > '9')
                    {
                        break;
                    }
                    x = x * 10 + *c++ - '0';
                }

                if (x != 0 && *c++ == ',')
                {
                    for (int i=0; i < 4; i++)
                    {
                        if (*c < '0' || *c > '9')
                        {
                            break;
                        }
                        y = y * 10 + *c++ - '0';
                    }   
                }

                if (x != 0 && y != 0 && x <= UnicamBase->u_FullSize.width && y <= UnicamBase->u_FullSize.height)
                {
                    UnicamBase->u_Size.width = x;
                    UnicamBase->u_Size.height = y;
                }
            }

            bug("[unicam] Displayed size: %ld x %ld\n", UnicamBase->u_Size.width, UnicamBase->u_Size.height);

            if ((cmd = FindToken(cmdline, "unicam.offset=")))
            {
                ULONG x = 0, y = 0;
                const char *c = &cmd[14];

                for (int i=0; i < 4; i++)
                {
                    if (*c < '0' || *c > '9')
                    {
                        break;
                    }
                    x = x * 10 + *c++ - '0';
                }

                if (*c++ == ',')
                {
                    for (int i=0; i < 4; i++)
                    {
                        if (*c < '0' || *c > '9')
                        {
                            break;
                        }
                        y = y * 10 + *c++ - '0';
                    }   
                }

                if ((x + UnicamBase->u_Size.width) <= UnicamBase->u_FullSize.width)
                {
                    UnicamBase->u_Offset.x = x;
                }

                if ((y + UnicamBase->u_Size.height) <= UnicamBase->u_FullSize.height)
                {
                    UnicamBase->u_Offset.y = y;
                }
            }

            bug("[unicam] Display offset: %ld, %ld\n", UnicamBase->u_Offset.x, UnicamBase->u_Offset.y);

            key = DT_OpenKey("/gpu");
            if (key)
            {
                const char *comp = DT_GetPropValue(DT_FindProperty(key, "compatible"));

                if (comp != NULL)
                {
                    if (_strcmp("brcm,bcm2711-vc5", comp) == 0)
                    {
                        UnicamBase->u_IsVC6 = 1;
                        bug("[unicam] VC6 detected\n");
                    }
                }
            }

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

                        const ULONG * siz = GetPropValueRecursive(key, "#size-cells", DeviceTreeBase);
                        const ULONG * addr = GetPropValueRecursive(key, "#address-cells", DeviceTreeBase);

                        if (siz != NULL)
                            size_cells = *siz;
                        
                        if (addr != NULL)
                            address_cells = *addr;

                        const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "reg"));

                        UnicamBase->u_MailBox = (APTR)reg[address_cells - 1];

                        DT_CloseKey(key);
                    }
                }
                DT_CloseKey(key);
            }

            /* Open /soc key and learn about VC4 to CPU mapping. Use it to adjust the addresses obtained above */
            key = DT_OpenKey("/soc");
            if (key)
            {
                int size_cells = 1;
                int address_cells = 1;
                int cpu_address_cells = 1;

                const ULONG * siz = GetPropValueRecursive(key, "#size-cells", DeviceTreeBase);
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

                if (UnicamBase->u_MailBox != 0) {
                    UnicamBase->u_MailBox = (APTR)((ULONG)UnicamBase->u_MailBox - phys_vc4 + phys_cpu);
                }

                UnicamBase->u_PeriphBase = (APTR)phys_cpu;

                DT_CloseKey(key);
            }

            bug("[unicam] Periph base: %08lx, Mailbox: %08lx\n", (ULONG)UnicamBase->u_PeriphBase, (ULONG)UnicamBase->u_MailBox);

            UnicamBase->u_ReceiveBuffer = NULL;
            /* Get location of receive buffer. If it does not exist, reserve it now */
            key = DT_OpenKey("/emu68");
            if (key)
            {
                const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "unicam-mem"));

                if (reg != NULL)
                {
                    UnicamBase->u_ReceiveBuffer = (APTR)reg[0];
                    UnicamBase->u_ReceiveBufferSize = reg[1];
                }

                DT_CloseKey(key);
            }

            if (UnicamBase->u_ReceiveBuffer == NULL)
            {
                const ULONG size = sizeof(ULONG) * UNICAM_HEIGHT * UNICAM_WIDTH;
                
                UnicamBase->u_ReceiveBuffer = AllocMem(size, MEMF_FAST);
                UnicamBase->u_ReceiveBufferSize = size;
            }

            bug("[unicam] Receive buffer: %08lx, size: %08lx\n", (ULONG)UnicamBase->u_ReceiveBuffer, UnicamBase->u_ReceiveBufferSize);

            AddResource(UnicamBase);

            if (start_on_boot)
            {
                LONG kernel_b = (UnicamBase->u_KernelB * 256) / 1000;
                LONG kernel_c = (UnicamBase->u_KernelC * 256) / 1000;

                UnicamBase->u_UnicamKernel = 0x300;
                ULONG *dlistPtr = (ULONG *)((ULONG)UnicamBase->u_PeriphBase + 
                    (UnicamBase->u_IsVC6 ? 0x00404000 : 0x00402000));

                bug("[unicam] DisplayList at %08lx\n", (ULONG)dlistPtr);

                UnicamStart(UnicamBase->u_ReceiveBuffer, 1, 
                    UnicamBase->u_Mode, 
                    UnicamBase->u_FullSize.width, UnicamBase->u_FullSize.height,
                    UnicamBase->u_BPP);

                while (UnicamBase->u_DisplaySize.width == 0 || UnicamBase->u_DisplaySize.height == 0)
                {
                    UnicamBase->u_DisplaySize = get_display_size(UnicamBase);
                }

                if (UnicamBase->u_Smooth)
                {
                    compute_scaling_kernel(&dlistPtr[UnicamBase->u_UnicamKernel], kernel_b, kernel_c);
                }
                else
                {
                    compute_nearest_neighbour_kernel(&dlistPtr[UnicamBase->u_UnicamKernel]);
                }

                if (UnicamBase->u_IsVC6)
                {
                    VC6_ConstructUnicamDL(UnicamBase, UnicamBase->u_UnicamKernel);
                }
                else
                {
                    VC4_ConstructUnicamDL(UnicamBase, UnicamBase->u_UnicamKernel);
                }

                *(volatile uint32_t *)(UnicamBase->u_PeriphBase + 0x00400024) = LE32(UnicamBase->u_UnicamDL);
            }

            binding.cb_ConfigDev->cd_Flags &= ~CDF_CONFIGME;
            binding.cb_ConfigDev->cd_Driver = UnicamBase;
        }

        CloseLibrary((struct Library *)ExpansionBase);
    }

    return  UnicamBase;
}

static void putch(REGARG(UBYTE data, "d0"), REGARG(APTR ignore, "a3"))
{
    (void)ignore;
    *(UBYTE*)0xdeadbeef = data;
}

void kprintf(REGARG(const char * msg, "a0"), REGARG(void * args, "a1")) 
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    RawDoFmt(msg, args, (APTR)putch, NULL);
}

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


CONST_STRPTR FindToken(CONST_STRPTR string, CONST_STRPTR token)
{
    CONST_STRPTR ret = NULL;

    if (string)
    {
        do {
            while (*string == ' ' || *string == '\t') {
                string++;
            }

            if (*string == 0)
                break;

            for (int i=0; token[i] != 0; i++)
            {
                if (string[i] != token[i])
                {
                    break;
                }

                if (token[i] == '=') {
                    ret = string;
                    break;
                }

                if (string[i+1] == 0 || string[i+1] == ' ' || string[i+1] == '\t') {
                    ret = string;
                    break;
                }
            }

            if (ret)
                break;

            while(*string != 0) {
                if (*string != ' ' && *string != '\t')
                    string++;
                else break;
            }

        } while(!ret && *string != 0);
    }
    return ret;
}

int _strcmp(const char *s1, const char *s2)
{
    while (*s1 == *s2++)
        if (*s1++ == '\0')
            return (0);
    return (*(const unsigned char *)s1 - *(const unsigned char *)(s2 - 1));
}
