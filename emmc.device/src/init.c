
#include <libraries/configregs.h>
#include <libraries/configvars.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/devicetree.h>
#include <inline/alib.h>

#include "emmc.h"
#include "findtoken.h"

extern const char deviceName[];
extern const char deviceIdString[];

void kprintf(const char * msg asm("a0"), void * args asm("a1")) 
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

void delay(ULONG us, struct EMMCBase *EMMCBase)
{
#if USE_BUSY_TIMER
    ULONG timer = LE32(*(volatile ULONG*)0xf2003004);
    ULONG end = timer + us;

    if (end < timer) {
        while (end < LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
    }
    while (end > LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
#else
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;
    EMMCBase->emmc_Port.mp_SigTask = FindTask(NULL);
    EMMCBase->emmc_TimeReq.tr_time.tv_micro = us % 1000000;
    EMMCBase->emmc_TimeReq.tr_time.tv_secs = us / 1000000;
    EMMCBase->emmc_TimeReq.tr_node.io_Command = TR_ADDREQUEST;

    DoIO((struct IORequest *)&EMMCBase->emmc_TimeReq);
#endif
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*(const unsigned char *)s1 - *(const unsigned char *)(s2 - 1));
}

APTR Init(struct ExecBase *SysBase asm("a6"))
{
    struct DeviceTreeBase *DeviceTreeBase = NULL;
    struct ExpansionBase *ExpansionBase = NULL;
    struct EMMCBase *EMMCBase = NULL;
    struct CurrentBinding binding;

    bug("[brcm-emmc] Init\n");

    DeviceTreeBase = OpenResource("devicetree.resource");

    if (DeviceTreeBase != NULL)
    {
        APTR base_pointer = NULL;
    
        ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 0);
        GetCurrentBinding(&binding, sizeof(binding));

        base_pointer = AllocMem(BASE_NEG_SIZE + BASE_POS_SIZE, MEMF_PUBLIC | MEMF_CLEAR);

        if (base_pointer != NULL)
        {
            APTR key;
            ULONG relFuncTable[7];
            ULONG disabled = 0;
            
            relFuncTable[0] = (ULONG)&EMMC_Open;
            relFuncTable[1] = (ULONG)&EMMC_Close;
            relFuncTable[2] = (ULONG)&EMMC_Expunge;
            relFuncTable[3] = (ULONG)&EMMC_ExtFunc;
            relFuncTable[4] = (ULONG)&EMMC_BeginIO;
            relFuncTable[5] = (ULONG)&EMMC_AbortIO;
            relFuncTable[6] = (ULONG)-1;

            EMMCBase = (struct EMMCBase *)((UBYTE *)base_pointer + BASE_NEG_SIZE);
            MakeFunctions(EMMCBase, relFuncTable, 0);

            EMMCBase->emmc_ManuID[0x01] = "Panasoni";
            EMMCBase->emmc_ManuID[0x02] = "Toshiba ";
            EMMCBase->emmc_ManuID[0x03] = "SanDisk ";
            EMMCBase->emmc_ManuID[0x08] = "SiliconP";
            EMMCBase->emmc_ManuID[0x18] = "Infineon";
            EMMCBase->emmc_ManuID[0x1b] = "Samsung ";
            EMMCBase->emmc_ManuID[0x1c] = "Transcnd";
            EMMCBase->emmc_ManuID[0x1d] = "AData   ";
            EMMCBase->emmc_ManuID[0x1e] = "Transcnd";
            EMMCBase->emmc_ManuID[0x1f] = "Kingston";
            EMMCBase->emmc_ManuID[0x27] = "Phison  ";
            EMMCBase->emmc_ManuID[0x28] = "Lexar   ";
            EMMCBase->emmc_ManuID[0x30] = "SanDisk ";
            EMMCBase->emmc_ManuID[0x31] = "SiliconP";
            EMMCBase->emmc_ManuID[0x41] = "Kingston";
            EMMCBase->emmc_ManuID[0x33] = "STMicro ";
            EMMCBase->emmc_ManuID[0x6f] = "STMicro ";
            EMMCBase->emmc_ManuID[0x74] = "Transcnd";
            EMMCBase->emmc_ManuID[0x76] = "Patriot ";
            EMMCBase->emmc_ManuID[0x82] = "Sony    ";
            EMMCBase->emmc_ManuID[0x89] = "Unknown ";
            EMMCBase->emmc_ManuID[0x9f] = "GoodRAM ";

            EMMCBase->emmc_ConfigDev = binding.cb_ConfigDev;
            EMMCBase->emmc_ROMBase = binding.cb_ConfigDev->cd_BoardAddr;

            EMMCBase->emmc_RequestBase = AllocMem(256*4, MEMF_FAST);
            EMMCBase->emmc_Request = (ULONG *)(((intptr_t)EMMCBase->emmc_RequestBase + 127) & ~127);

            EMMCBase->emmc_Device.dd_Library.lib_Node.ln_Type = NT_DEVICE;
            EMMCBase->emmc_Device.dd_Library.lib_Node.ln_Pri = EMMC_PRIORITY;
            EMMCBase->emmc_Device.dd_Library.lib_Node.ln_Name = (STRPTR)deviceName;

            EMMCBase->emmc_Device.dd_Library.lib_NegSize = BASE_NEG_SIZE;
            EMMCBase->emmc_Device.dd_Library.lib_PosSize = BASE_POS_SIZE;
            EMMCBase->emmc_Device.dd_Library.lib_Version = EMMC_VERSION;
            EMMCBase->emmc_Device.dd_Library.lib_Revision = EMMC_REVISION;
            EMMCBase->emmc_Device.dd_Library.lib_IdString = (STRPTR)deviceIdString;

            EMMCBase->emmc_SysBase = SysBase;
            EMMCBase->emmc_DeviceTreeBase = DeviceTreeBase;

            InitSemaphore(&EMMCBase->emmc_Lock);
            EMMCBase->emmc_Port.mp_Flags = PA_SIGNAL;
            EMMCBase->emmc_Port.mp_SigBit = SIGBREAKB_CTRL_C;
            NewList(&EMMCBase->emmc_Port.mp_MsgList);

            EMMCBase->emmc_TimeReq.tr_node.io_Message.mn_ReplyPort = &EMMCBase->emmc_Port;
            OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)&EMMCBase->emmc_TimeReq, 0);

            SumLibrary((struct Library*)EMMCBase);

            bug("[brcm-emmc] DeviceBase at %08lx\n", (ULONG)EMMCBase);

            const char *cmdline = DT_GetPropValue(DT_FindProperty(DT_OpenKey("/chosen"), "bootargs"));
            const char *cmd;

            EMMCBase->emmc_HideUnit0 = 0;
            EMMCBase->emmc_ReadOnlyUnit0 = 1;

            if ((cmd = FindToken(cmdline, "emmc.verbose=")))
            {
                ULONG verbose = 0;

                for (int i=0; i < 3; i++)
                {
                    if (cmd[13 + i] < '0' || cmd[13 + i] > '9')
                        break;

                    verbose = verbose * 10 + cmd[13 + i] - '0';
                }

                if (verbose > 10)
                    verbose = 10;

                bug("[brcm-emmc] Requested verbosity level: %ld\n", verbose);

                EMMCBase->emmc_Verbose = (UBYTE)verbose;
            }

            if ((cmd = FindToken(cmdline, "emmc.unit0=")))
            {
                if (cmd[11] == 'r' && cmd[12] == 'o' && (cmd[13] == 0 || cmd[13] == ' ')) {
                    EMMCBase->emmc_ReadOnlyUnit0 = 1;
                    EMMCBase->emmc_HideUnit0 = 0;
                    bug("[brcm-emmc] Unit 0 is read only\n");
                }
                else if (cmd[11] == 'r' && cmd[12] == 'w' && (cmd[13] == 0 || cmd[13] == ' ')) {
                    EMMCBase->emmc_ReadOnlyUnit0 = 0;
                    EMMCBase->emmc_HideUnit0 = 0;
                    bug("[brcm-emmc] Unit 0 is writable\n");
                }
                else if (cmd[11] == 'o' && cmd[12] == 'f' && cmd[13] == 'f' && (cmd[14] == 0 || cmd[14] == ' ')) {
                    EMMCBase->emmc_HideUnit0 = 1;
                    bug("[brcm-emmc] Unit 0 is hidden\n");
                }
            }

            if (FindToken(cmdline, "emmc.low_speed"))
            {
                bug("[brcm-emmc] 50MHz mode disabled per command line\n");

                EMMCBase->emmc_DisableHighSpeed = 1;
            }

            if ((cmd = FindToken(cmdline, "emmc.clock=")))
            {
                ULONG clock = 0;

                for (int i=0; i < 3; i++)
                {
                    if (cmd[11 + i] < '0' || cmd[11 + i] > '9')
                        break;

                    clock = clock * 10 + cmd[11 + i] - '0';
                }

                if (clock > 0 && clock < 200)
                {
                    bug("[brcm-emmc] Overclocking to %ld MHz requested\n", clock);
                    EMMCBase->emmc_Overclock = 1000000 * clock;
                }
            }

            if (FindToken(cmdline, "emmc.disable"))
            {
                bug("[brcm-emmc] brcm-emmc.device disabled by user\n");

                disabled = 1;
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

                        EMMCBase->emmc_MailBox = (APTR)reg[address_cells - 1];

                        DT_CloseKey(key);
                    }
                }
                DT_CloseKey(key);
            }

            /* Open /aliases and find out the "link" to the emmc */
            key = DT_OpenKey("/aliases");
            if (key)
            {
                CONST_STRPTR mmc_alias = DT_GetPropValue(DT_FindProperty(key, "emmc2bus"));

                DT_CloseKey(key);
               
                if (mmc_alias != NULL)
                {
                    /* Open the alias and find out the MMIO VC4 physical base address */
                    APTR emmckey = DT_OpenKey(mmc_alias);
                    APTR key = NULL;

                    if (emmckey)
                    {
                        while((key = DT_GetChild(emmckey, key)))
                        {
                            bug("[brcm-emmc] Checking key %s\n", (ULONG)DT_GetKeyName(key));
                            if (strcmp(DT_GetKeyName(key), "mmc") == '@')
                            {
                                bug("[brcm-emmc] Key match!\n");
                                break;
                            }
                        }

                        if (key) {
                            int size_cells = 1;
                            int address_cells = 1;

                            const ULONG * siz = GetPropValueRecursive(key, "#size-cells", DeviceTreeBase);
                            const ULONG * addr = GetPropValueRecursive(key, "#address-cells", DeviceTreeBase);

                            if (siz != NULL)
                                size_cells = *siz;
                            
                            if (addr != NULL)
                                address_cells = *addr;

                            const ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "reg"));
                            EMMCBase->emmc_Regs = (APTR)reg[address_cells - 1];
                            DT_CloseKey(key);
                        }
                        DT_CloseKey(emmckey);
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

                if (EMMCBase->emmc_MailBox != 0)
                    EMMCBase->emmc_MailBox = (APTR)((ULONG)EMMCBase->emmc_MailBox - phys_vc4 + phys_cpu);
                if (EMMCBase->emmc_Regs != 0)
                    EMMCBase->emmc_Regs = (APTR)((ULONG)EMMCBase->emmc_Regs - phys_vc4 + phys_cpu);

                if (EMMCBase->emmc_Verbose > 0) {
                    bug("[brcm-emmc] Mailbox at %08lx\n", (ULONG)EMMCBase->emmc_MailBox);
                    bug("[brcm-emmc] EMMC regs at %08lx\n", (ULONG)EMMCBase->emmc_Regs);
                }

                DT_CloseKey(key);
            }

            /* If both sd_MailBox and sd_SDHC are set, everything went OK and now we can add the device */
            if (EMMCBase->emmc_MailBox != NULL && EMMCBase->emmc_Regs != NULL && disabled == 0)
            {
                AddDevice((struct Device *)EMMCBase);

                /* Enable eMMC clock */
                set_clock_state(0x0c, 1, EMMCBase);
                set_clock_state(1, 1, EMMCBase);

                if (emmc_card_init(EMMCBase) == 0)
                {
                    uint8_t buff[512];
                    /* Initializataion was successful. Read parition table and create the units */

                    if (EMMCBase->emmc_Verbose)
                    {
                        bug("[brcm-emmc] Attempting to read card capacity\n");
                    }
                    EMMCBase->emmc_Buffer = buff;
                    EMMCBase->emmc_BlocksToTransfer = 1;

                    if (EMMCBase->emmc_isMicroSD)
                    {
                        emmc_cmd(DESELECT_CARD, 0, 1000000, EMMCBase);
                        emmc_cmd(SEND_CSD, EMMCBase->emmc_CardRCA << 16, 1000000, EMMCBase);
                    }
                    else
                    {
                        emmc_cmd(SEND_EXT_CSD, 0, 500000, EMMCBase);
                    }

                    if(!FAIL(EMMCBase))
                    {
                        ULONG block_count = EMMCBase->emmc_isMicroSD ? 1024 * ((EMMCBase->emmc_Res1 >> 8) & 0x3fffff) + 1024 : LE32(*(ULONG*)(APTR)&buff[212]);
                        EMMCBase->emmc_UnitCount = 1;
                        EMMCBase->emmc_Units[0] = AllocMem(sizeof(struct EMMCUnit), MEMF_PUBLIC | MEMF_CLEAR);
                        EMMCBase->emmc_Units[0]->su_StartBlock = 0;
                        EMMCBase->emmc_Units[0]->su_BlockCount = block_count;
                        EMMCBase->emmc_Units[0]->su_Base = EMMCBase;
                        EMMCBase->emmc_Units[0]->su_UnitNum = 0;

                        if (EMMCBase->emmc_Verbose) {
                            bug("[brcm-emmc] Reported card capacity: %ld MB (%ld sectors)\n", EMMCBase->emmc_Units[0]->su_BlockCount / 2048,
                                EMMCBase->emmc_Units[0]->su_BlockCount);
                        }

                        if (EMMCBase->emmc_Verbose > 1)
                            bug("[brcm-emmc] Attempting to read block at 0\n");
                        
                        ULONG ret = emmc_read(buff, 512, 0, EMMCBase);
                        
                        if (EMMCBase->emmc_Verbose > 1)
                            bug("[brcm-emmc] Result %ld\n", ret);

                        if (ret > 0)
                        {
                            for (int i=0; i < 4; i++) {
                                uint8_t *b = &buff[0x1be + 16 * i];
                                ULONG p0_Type = b[4];
                                ULONG p0_Start = b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24);
                                ULONG p0_Len = b[12] | (b[13] << 8) | (b[14] << 16) | (b[15] << 24);

                                // Partition does exist. List it.
                                if (p0_Type != 0) {
                                    if (EMMCBase->emmc_Verbose) {
                                        bug("[brcm-emmc] Partition%ld: type 0x%02lx, start 0x%08lx, length 0x%08lx\n", i, p0_Type, p0_Start, p0_Len);
                                    }

                                    // Partition type 0x76 (Amithlon-like) creates new virtual unit with given capacity
                                    if (p0_Type == 0x76) {
                                        //emmc_write(0x20000000, 0x20000, p0_Start, EMMCBase);
                                        struct EMMCUnit *unit = AllocMem(sizeof(struct EMMCUnit), MEMF_PUBLIC | MEMF_CLEAR);
                                        unit->su_StartBlock = p0_Start;
                                        unit->su_BlockCount = p0_Len;
                                        unit->su_Base = EMMCBase;
                                        unit->su_UnitNum = EMMCBase->emmc_UnitCount;
                                        
                                        EMMCBase->emmc_Units[EMMCBase->emmc_UnitCount++] = unit;
                                    }
                                }
                            }
                        }

                        if (EMMCBase->emmc_Verbose)
                            bug("[brcm-emmc] Init complete.\n");

                        /* Initialization is complete. Create all tasks for the units now */
                        for (int unit = 0; unit < EMMCBase->emmc_UnitCount; unit++)
                        {
                            if (unit == 0 && EMMCBase->emmc_HideUnit0)
                                continue;

                            APTR entry = (APTR)UnitTask;
                            struct Task *task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
                            struct MemList *ml = AllocMem(sizeof(struct MemList) + 2*sizeof(struct MemEntry), MEMF_PUBLIC | MEMF_CLEAR);
                            ULONG *stack = AllocMem(UNIT_TASK_STACKSIZE * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                            const char unit_name[] = "brcm-emmc unit 0";
                            STRPTR unit_name_copy = AllocMem(sizeof(unit_name), MEMF_PUBLIC | MEMF_CLEAR);
                            CopyMem((APTR)unit_name, unit_name_copy, sizeof(unit_name));

                            ml->ml_NumEntries = 3;
                            ml->ml_ME[0].me_Un.meu_Addr = task;
                            ml->ml_ME[0].me_Length = sizeof(struct Task);

                            ml->ml_ME[1].me_Un.meu_Addr = &stack[0];
                            ml->ml_ME[1].me_Length = UNIT_TASK_STACKSIZE * sizeof(ULONG);

                            ml->ml_ME[2].me_Un.meu_Addr = unit_name_copy;
                            ml->ml_ME[2].me_Length = sizeof(unit_name);

                            unit_name_copy[sizeof(unit_name) - 2] += unit;

                            task->tc_UserData = EMMCBase->emmc_Units[unit];
                            task->tc_SPLower = &stack[0];
                            task->tc_SPUpper = &stack[UNIT_TASK_STACKSIZE];
                            task->tc_SPReg = task->tc_SPUpper;

                            task->tc_Node.ln_Name = unit_name_copy;
                            task->tc_Node.ln_Type = NT_TASK;
                            task->tc_Node.ln_Pri = UNIT_TASK_PRIORITY;

                            NewList(&task->tc_MemEntry);
                            AddHead(&task->tc_MemEntry, &ml->ml_Node);

                            EMMCBase->emmc_Units[unit]->su_Caller = FindTask(NULL);

                            if (unit == 0 && EMMCBase->emmc_ReadOnlyUnit0)
                                EMMCBase->emmc_Units[unit]->su_ReadOnly = 1;

                            AddTask(task, entry, NULL);
                            Wait(SIGBREAKF_CTRL_C);
                        }
                    }
                }

                binding.cb_ConfigDev->cd_Flags &= ~CDF_CONFIGME;
                binding.cb_ConfigDev->cd_Driver = EMMCBase;
            }
            else
            {
                /*  
                    Something failed, device will not be added to the system, free allocated memory and 
                    return NULL instead
                */
                CloseDevice((struct IORequest *)&EMMCBase->emmc_TimeReq);
                FreeMem(EMMCBase->emmc_RequestBase, 256*4);
                FreeMem(base_pointer, BASE_NEG_SIZE + BASE_POS_SIZE);
                EMMCBase = NULL;
            }
        }

        CloseLibrary((struct Library *)ExpansionBase);
    }

    return  EMMCBase;
}
