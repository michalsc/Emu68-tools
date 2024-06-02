#include <clib/alib_protos.h>
#include <exec/resident.h>
#include <exec/nodes.h>
#include <exec/devices.h>
#include <exec/errors.h>

#include <utility/tagitem.h>

#include <devices/sana2.h>
#include <devices/sana2specialstats.h>
#include <devices/sana2wireless.h>
#include <devices/newstyle.h>

#if defined(__INTELLISENSE__)
#include <clib/exec_protos.h>
#include <clib/utility_protos.h>
#include <clib/dos_protos.h>
#include <clib/timer_protos.h>
#else
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/dos.h>
#include <proto/timer.h>
#endif

#include <stdint.h>

#include "wifipi.h"
#include "packet.h"
#include "brcm.h"
#include "brcm_wifi.h"

#define D(x) x
#define UNIT_STACK_SIZE (32768 / sizeof(ULONG))
#define UNIT_TASK_PRIORITY 10

void UnitTask(struct WiFiUnit *unit, struct Task *parent)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct MsgPort *port;
    struct timerequest *tr;
    ULONG sigset;

    D(bug("[WiFi.0] Unit task starting\n"));

    port = CreateMsgPort();
    tr = (struct timerequest *)CreateIORequest(port, sizeof(struct timerequest));
    unit->wu_CmdQueue = CreateMsgPort();
    unit->wu_ScanQueue = CreateMsgPort();

    unit->wu_Task = FindTask(NULL);

    if (port == NULL || tr == NULL || unit->wu_CmdQueue == NULL) // || unit->wu_WriteQueue == NULL)
    {
        D(bug("[WiFi.0] Failed to create requested MsgPorts\n"));
        
        DeleteMsgPort(unit->wu_ScanQueue);
        DeleteMsgPort(unit->wu_CmdQueue);
        DeleteIORequest((struct IORequest *)tr);
        DeleteMsgPort(port);
        unit->wu_CmdQueue = NULL;
        return;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_VBLANK, &tr->tr_node, 0))
    {
        D(bug("[WiFi.0] Failed to open timer.device\n"));
        DeleteIORequest(&tr->tr_node);
        DeleteMsgPort(port);
        DeleteMsgPort(unit->wu_CmdQueue);
        DeleteMsgPort(unit->wu_ScanQueue);
        unit->wu_CmdQueue = NULL;
        return;
    }

    unit->wu_TimerBase = (struct TimerBase *)tr->tr_node.io_Device;
    unit->wu_Unit.unit_MsgPort.mp_SigTask = FindTask(NULL);
    unit->wu_Unit.unit_flags = PA_IGNORE;
    NewList(&unit->wu_Unit.unit_MsgPort.mp_MsgList);

    /* Let timer run every 10 seconds, this will trigger scan for networks */
    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_sec = 1;
    tr->tr_time.tv_micro = 0;
    SendIO(&tr->tr_node);

    /* Signal parent that Unit task is up and running now */
    Signal(parent, SIGBREAKF_CTRL_F);
    
    do {
        sigset = Wait((1 << unit->wu_CmdQueue->mp_SigBit) |
                      (1 << port->mp_SigBit) |
                      SIGBREAKF_CTRL_C);
        
        // Handle periodic request
        if (sigset & (1 << port->mp_SigBit))
        {
            // Check if IO really completed. If yes, remove it from the queue
            if (CheckIO(&tr->tr_node))
            {
                WaitIO(&tr->tr_node);
            }
#if 0
            // No network is in progress, decrease delay and start scanner
            if (!WiFiBase->w_NetworkScanInProgress)
            {
                if (scanDelay == 20)
                {
                    struct WiFiNetwork *network, *next;
                    ObtainSemaphore(&WiFiBase->w_NetworkListLock);
                    ForeachNodeSafe(&WiFiBase->w_NetworkList, network, next)
                    {
                        // If network wasn't available for more than 60 seconds after scan, remove it
                        if (network->wn_LastUpdated++ > 6) {
                            Remove((struct Node*)network);
                            if (network->wn_IE)
                                FreePooled(WiFiBase->w_MemPool, network->wn_IE, network->wn_IELength);
                            FreePooled(WiFiBase->w_MemPool, network, sizeof(struct WiFiNetwork));
                        }
                        #if  0
                        else
                        {
                            D(bug("[WiFi.0]   SSID: '%-32s', BSID: %02lx:%02lx:%02lx:%02lx:%02lx:%02lx, Type: %s, Channel %ld, Spec:%04lx, RSSI: %ld\n",
                                (ULONG)network->wn_SSID, network->wn_BSID[0], network->wn_BSID[1], network->wn_BSID[2],
                                network->wn_BSID[3], network->wn_BSID[4], network->wn_BSID[5], 
                                network->wn_ChannelInfo.ci_Band == BRCMU_CHAN_BAND_2G ? (ULONG)"2.4GHz" : (ULONG)"5GHz",
                                network->wn_ChannelInfo.ci_CHNum, network->wn_ChannelInfo.ci_CHSpec,
                                network->wn_RSSI
                            ));
                        }
                        #endif
                    }
                    ReleaseSemaphore(&WiFiBase->w_NetworkListLock);
                }

                if (scanDelay) scanDelay--;

                if (scanDelay == 0)
                {
                    StartNetworkScan(WiFiBase->w_SDIO);
                }
            }
            else
            {
                scanDelay = 20;
            }
#endif
            // Restart timer request
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_sec = 1;
            tr->tr_time.tv_micro = 0;
            SendIO(&tr->tr_node);
        }

        // IO queue got a new message
        if (sigset & (1 << unit->wu_CmdQueue->mp_SigBit))
        {
            struct IOSana2Req *io;
            
            // Drain command queue and process it
            while ((io = (struct IOSana2Req *)GetMsg(unit->wu_CmdQueue)))
            {
                // Requests are handled one after another while holding a lock
                ObtainSemaphore(&unit->wu_Lock);
                HandleRequest(io);
                ReleaseSemaphore(&unit->wu_Lock);
            }
        }
#if 0
        if (sigset & (1 << unit->wu_WriteQueue->mp_SigBit))
        {
            struct IOSana2Req *io;
            
            // Drain Data write queue and process it
            while ((io = (struct IOSana2Req *)GetMsg(unit->wu_WriteQueue)))
            {
                /* ... */
            }
        }
#endif
        // If CtrlC is sent, abort timer request
        if (sigset & SIGBREAKF_CTRL_C)
        {
            // Abort timer IO
            AbortIO(&tr->tr_node);
            WaitIO(&tr->tr_node);
        }
    } while ((sigset & SIGBREAKF_CTRL_C) == 0);

    CloseDevice(&tr->tr_node);
    DeleteIORequest(&tr->tr_node);
    DeleteMsgPort(port);
    DeleteMsgPort(unit->wu_CmdQueue);
    DeleteMsgPort(unit->wu_ScanQueue);
    unit->wu_Task = NULL;
}

void StartUnit(struct WiFiUnit *unit)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;

#if 0
    if (WiFiBase->w_DosBase)
    {
        struct Library *DOSBase = (struct Library *)WiFiBase->w_DosBase;

        UBYTE buffer[4];

        /* Get the variable into a too small buffer. Required length will be returned in IoErr() */
        if (GetVar("SYS/Wireless.prefs", buffer, 4, GVF_BINARY_VAR) > 0)
        {
            ULONG requiredLength = IoErr();
            UBYTE *config = AllocPooled(WiFiBase->w_MemPool, requiredLength + 1);
            GetVar("SYS/Wireless.prefs", config, requiredLength + 1, GVF_BINARY_VAR);
            WiFiBase->w_NetworkConfigVar = config;
            WiFiBase->w_NetworkConfigLength = requiredLength;
            ParseConfig(WiFiBase);
        }
    }
#endif

    PacketGetVar(WiFiBase->w_SDIO, "cur_etheraddr", unit->wu_OrigEtherAddr, 6);

    D(bug("[WiFi.0] Ethernet addr: %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
        unit->wu_OrigEtherAddr[0], unit->wu_OrigEtherAddr[1], unit->wu_OrigEtherAddr[2],
        unit->wu_OrigEtherAddr[3], unit->wu_OrigEtherAddr[4], unit->wu_OrigEtherAddr[5]));

    /* Don't set EtherAddr yet. */
    _bzero(unit->wu_EtherAddr, 6);
    
    unit->wu_Flags |= IFF_STARTED;
}

void StartUnitTask(struct WiFiUnit *unit)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    APTR entry = (APTR)UnitTask;
    struct Task *task;
    struct MemList *ml;
    ULONG *stack;
    static const char task_name[] = "WiFiPi Unit";

    D(bug("[WiFi] StartUnitTask\n"));

    // Get all memory we need for the receiver task
    task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
    ml = AllocMem(sizeof(struct MemList) + sizeof(struct MemEntry), MEMF_PUBLIC | MEMF_CLEAR);
    stack = AllocMem(UNIT_STACK_SIZE * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);

    // Prepare mem list, put task and its stack there
    ml->ml_NumEntries = 2;
    ml->ml_ME[0].me_Un.meu_Addr = task;
    ml->ml_ME[0].me_Length = sizeof(struct Task);

    ml->ml_ME[1].me_Un.meu_Addr = &stack[0];
    ml->ml_ME[1].me_Length = UNIT_STACK_SIZE * sizeof(ULONG);

    // Set up stack
    task->tc_SPLower = &stack[0];
    task->tc_SPUpper = &stack[UNIT_STACK_SIZE];

    // Push ThisTask and SDIO on the stack
    stack = (ULONG *)task->tc_SPUpper;
    *--stack = (ULONG)FindTask(NULL);
    *--stack = (ULONG)unit;
    task->tc_SPReg = stack;

    task->tc_Node.ln_Name = (char *)task_name;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Pri = UNIT_TASK_PRIORITY;

    NewMinList((struct MinList *)&task->tc_MemEntry);
    AddHead(&task->tc_MemEntry, &ml->ml_Node);

    D(bug("[WiFi] UnitTask starting\n"));

    AddTask(task, entry, NULL);

    // Wait for a signal
    Wait(SIGBREAKF_CTRL_F);
}

static const UWORD WiFi_SupportedCommands[] = {
    CMD_FLUSH,
    CMD_READ,
    CMD_WRITE,

    S2_DEVICEQUERY,
    S2_GETSTATIONADDRESS,
    S2_CONFIGINTERFACE,
    S2_ADDMULTICASTADDRESS,
    S2_DELMULTICASTADDRESS,
    S2_MULTICAST,
    S2_BROADCAST,
    // S2_TRACKTYPE,
    // S2_UNTRACKTYPE,
    // S2_GETTYPESTATS,
//    S2_GETSPECIALSTATS, <-- not used yet!
    S2_GETGLOBALSTATS,
    S2_ONEVENT,
    S2_READORPHAN,
    S2_ONLINE,
    S2_OFFLINE,
    S2_ADDMULTICASTADDRESSES,
    S2_DELMULTICASTADDRESSES,

    S2_GETSIGNALQUALITY,
    S2_GETNETWORKS,
    S2_SETOPTIONS,
    S2_SETKEY,
    S2_GETNETWORKINFO,
    S2_GETCRYPTTYPES,
    //S2_GETRADIOBANDS,

    NSCMD_DEVICEQUERY,
    0
};

/* Mask of events known by the driver */
#define EVENT_MASK (S2EVENT_CONNECT | S2EVENT_DISCONNECT | S2EVENT_ONLINE   |  \
                    S2EVENT_OFFLINE | S2EVENT_ERROR      | S2EVENT_TX       |  \
                    S2EVENT_RX      | S2EVENT_BUFF       | S2EVENT_HARDWARE |  \
                    S2EVENT_SOFTWARE)

/* Report events to this unit */
void ReportEvents(struct WiFiUnit *unit, ULONG eventSet)
{
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;
    struct Opener *opener;

    /* Report event to every listener of every opener accepting the mask */
    Disable();
    ForeachNode(&unit->wu_Openers, opener)
    {
        struct IOSana2Req *io, *next;

        ForeachNodeSafe(&opener->o_EventListeners.mp_MsgList, io, next)
        {
            /* Check if event mask in WireError fits the events occured */
            if (io->ios2_WireError & eventSet)
            {
                /* We have a match. Leave only matching events in wire error */
                io->ios2_WireError &= eventSet;
                
                /* Reply it */
                Remove((struct Node *)io);
                ReplyMsg((struct Message *)io);
            }
        }
    }
    Enable();
}

static int Do_S2_GETCRYPTTYPES(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    if (io->ios2_Data)
    {
        UBYTE *types = AllocPooled(io->ios2_Data, 3);
        if (types == NULL)
        {
            io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
        }
        else
        {
            types[0] = S2ENC_CCMP;
            types[1] = S2ENC_TKIP;
            types[2] = S2ENC_WEP;
            io->ios2_StatData = types;
            io->ios2_DataLength = 3;
            io->ios2_Req.io_Error = 0;
        }
    }
    else
    {
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        io->ios2_WireError = S2WERR_GENERIC_ERROR;
    }
    return 1;
}

static int Do_S2_ONEVENT(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    ULONG preset;
    if (unit->wu_Flags & IFF_ONLINE) preset = S2EVENT_ONLINE;
    else preset = S2EVENT_OFFLINE;

    D(bug("[WiFi.0] S2_ONEVENT(%08lx)\n", io->ios2_WireError));

    /* If any unsupported events are requested, report an error */
    if (io->ios2_WireError & ~(EVENT_MASK))
    {
        io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
        io->ios2_WireError = S2WERR_BAD_EVENT;
        return 1;
    }

    /* If expected flags match preset, return back (almost) immediately */
    if (io->ios2_WireError & preset)
    {
        io->ios2_WireError &= preset;
        return 1;
    }
    else
    {
        D(bug("[WiFi] Event listener moved into list\n"));
        /* Remove QUICK flag and put message on event listener list */
        struct Opener *opener = io->ios2_BufferManagement;
        io->ios2_Req.io_Flags &= ~IOF_QUICK;
        PutMsg(&opener->o_EventListeners, (struct Message *)io);
        return 0;
    }
}

static int Do_S2_GETSIGNALQUALITY(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    D(bug("[WiFi.0] S2_GETSIGNALQUALITY\n"));

    /* If expected flags match preset, return back (almost) immediately */
    if ((unit->wu_Flags & IFF_ONLINE) == 0)
    {
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        io->ios2_WireError = S2WERR_UNIT_OFFLINE;
        return 1;
    }
    else if ((unit->wu_Flags & IFF_CONNECTED) == 0)
    {
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        io->ios2_WireError = S2WERR_NOT_CONFIGURED;
        return 1;
    }
    else
    {
        /* Remove QUICK flag and put message on event listener list */
        struct Sana2SignalQuality *quality = io->ios2_StatData;
        PacketCmdIntGet(WiFiBase->w_SDIO, BRCMF_C_GET_RSSI, (APTR)&quality->SignalLevel);
        D(bug("GET_RSSI ok\n"));
        PacketCmdIntGet(WiFiBase->w_SDIO, BRCMF_C_GET_PHY_NOISE, (APTR)&quality->NoiseLevel);
        D(bug("GET_PHY_NOISE ok\n"));

        D(bug("[WiFi.0] Signal: %ld, Noise: %ld\n", quality->SignalLevel, quality->NoiseLevel));
        return 1;
    }
}

void delay_us(ULONG us, struct WiFiBase *WiFiBase);

static int Do_S2_SETKEY(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    ULONG idx = io->ios2_WireError & 3;

    D(bug("[WiFi.0] S2_SETKEY\n"));

    D(bug("[WiFi.0]   Index: %ld\n", io->ios2_WireError));
    D(bug("[WiFi.0]   Enc.Type: %ld\n", io->ios2_PacketType));
    D(bug("[WiFi.0]   KeyLength: %ld\n", io->ios2_DataLength));

    D(bug("[WiFi.0]   Key: %08lx", (ULONG)io->ios2_Data));
    if (io->ios2_Data && io->ios2_DataLength)
    {
        for (ULONG i=0; i < io->ios2_DataLength; i++) {
            if (i == 0)
                bug(" (%02lx, ", ((UBYTE*)io->ios2_Data)[i]);
            else if (i == io->ios2_DataLength - 1)
                bug("%02lx)\n", ((UBYTE*)io->ios2_Data)[i]);
            else
                bug("%02lx, ", ((UBYTE*)io->ios2_Data)[i]);
        }
    }
    else { D(bug("\n")); }
    D(bug("[WiFi.0]   RX cnt: %08lx", (ULONG)io->ios2_StatData));
    if (io->ios2_StatData)
    {
        for (ULONG i=0; i < 6; i++) {
            if (i == 0)
                bug(" (%02lx, ", ((UBYTE*)io->ios2_StatData)[i]);
            else if (i == 5)
                bug("%02lx)\n", ((UBYTE*)io->ios2_StatData)[i]);
            else
                bug("%02lx, ", ((UBYTE*)io->ios2_StatData)[i]);
        }
    }
    else { D(bug("\n")); }

    if (unit->wu_Keys[idx].k_Key)
        FreeVecPooled(WiFiBase->w_MemPool, unit->wu_Keys[idx].k_Key);
    
    unit->wu_Keys[idx].k_Type = io->ios2_PacketType;
    unit->wu_Keys[idx].k_Length = io->ios2_DataLength;
    unit->wu_Keys[idx].k_RXCountHigh = LE32(*(ULONG*)((ULONG)io->ios2_StatData + 2));
    unit->wu_Keys[idx].k_RXCountLow = LE16(*(UWORD*)io->ios2_StatData);
    if (io->ios2_DataLength != 0)
    {
        unit->wu_Keys[idx].k_Key = AllocVecPooledClear(WiFiBase->w_MemPool, io->ios2_DataLength);
        if (unit->wu_Keys[idx].k_Key == NULL)
        {
            io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
            
            return 1;
        }
        CopyMem(io->ios2_Data, unit->wu_Keys[idx].k_Key, io->ios2_DataLength);
    }
    else
    {
        unit->wu_Keys[idx].k_Key = NULL;
    }

    /* If key length == 0 then actually clear the key */
    if (io->ios2_DataLength == 0)
    {
        struct bwfm_wsec_key key;
        _bzero(&key, sizeof(key));

        /* Set index and flags only, everything else remains empty */
        key.k_Index = LE32(io->ios2_WireError);
        key.k_Flags = LE32(WSEC_PRIMARY_KEY);

        PacketSetVar(WiFiBase->w_SDIO, "wsec_key", &key, sizeof(key));
    }
    else
    {
        ULONG wsec = 0;
        ULONG wsec_orig = 0;
        struct bwfm_wsec_key key;
        _bzero(&key, sizeof(key));
        
        UBYTE *rx_counter = io->ios2_StatData;

        /* Set index and primary key */
        key.k_Index = LE32(io->ios2_WireError);
        key.k_Flags = LE32(WSEC_PRIMARY_KEY);
        key.k_Length = LE32(io->ios2_DataLength);

        if (rx_counter != NULL)
        {
            key.k_RXIV.riv_High = (*(ULONG*)(rx_counter + 2));
            key.k_RXIV.riv_Low = (*(UWORD*)(rx_counter));
        }

        /* Copy key itself */
        CopyMem(io->ios2_Data, key.k_Data, io->ios2_DataLength);

        /* Key idx 0 is GTK, it needs AP's BSSID and flags zeroed */
        if (io->ios2_WireError == 0)
        {
            CopyMem(unit->wu_JoinParams.ej_Assoc.ap_BSSID, &key.k_EA, 6);
            key.k_Flags = 0;
        }

        switch (io->ios2_PacketType)
        {
            case S2ENC_NONE:
                break;
            case S2ENC_WEP:
                key.k_Algo = LE32(CRYPTO_ALGO_WEP128);
                wsec = WEP_ENABLED;
                break;
            case S2ENC_TKIP:
                key.k_Algo = LE32(CRYPTO_ALGO_TKIP);
                wsec = TKIP_ENABLED;
                break;
            case S2ENC_CCMP:
                key.k_Algo = LE32(CRYPTO_ALGO_AES_CCM);
                wsec = AES_ENABLED;
                break;
        }

        D(bug("[WiFi.0] Setting key (size %ld)\n", sizeof(key)));
        for (unsigned i=0; i < sizeof(key); i++)
        {
            if (i % 16 == 0)
                D(bug("[KEY] %03lx:", i));
            
            D(bug(" %02lx", ((UBYTE*)&key)[i]));
            if (i % 16 == 15)
                D(bug("\n"));
        }
        if (sizeof(key) & 15) D(bug("\n"));

        delay_us(1000, WiFiBase);

        PacketSetVar(WiFiBase->w_SDIO, "wsec_key", &key, sizeof(key));
        D(bug("[WiFi.0] Key set\n"));
        (void)wsec;
        (void)wsec_orig;
        #if 0
        PacketGetVar(WiFiBase->w_SDIO, "wsec", &wsec_orig, sizeof(ULONG));
        wsec_orig = LE32(wsec_orig);
        D(bug("[WiFi.0] Orig wsec: %08lx\n", wsec_orig));
        wsec_orig &= ~(TKIP_ENABLED | WEP_ENABLED | AES_ENABLED);
        wsec |= wsec_orig;
        D(bug("[WiFi.0] New wsec: %08lx\n", wsec));
        PacketSetVarInt(WiFiBase->w_SDIO, "wsec", wsec);
        #endif
    }

    return 1;
}

static int brcmf_valid_wpa_oui(UBYTE *oui, int is_rsn_ie)
{
    CONST_STRPTR o = is_rsn_ie ? RSN_OUI : WPA_OUI;

    for (int i=0; i < TLV_OUI_LEN; i++) if (oui[i] != o[i]) return 0;

    return 1;
}

static int Do_S2_SETOPTIONS(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct Library *UtilityBase = WiFiBase->w_UtilityBase;

    D(bug("[WiFi.0] S2_SETOPTIONS\n"));

    struct TagItem *ti = io->ios2_Data;
    UBYTE ie_found = 0;
    UBYTE *ie_b = NULL;
    ULONG off = 0;

    if (ti)
    {
        while (ti->ti_Tag != TAG_DONE)
        {
            D(bug("[WiFi.0]   Tag: %08lx, Data: %08lx\n", ti->ti_Tag, ti->ti_Data));
            ti++;
        }
    }

    /* Set ExtJoinParams now */
    _bzero(&unit->wu_JoinParams, sizeof(struct ExtJoinParams));
    
    /* Get SSID */
    if ((ti = FindTagItem(S2INFO_SSID, io->ios2_Data)))
    {
        STRPTR ssid = (STRPTR)ti->ti_Data;
        ULONG len = _strlen(ssid);
        unit->wu_JoinParams.ej_SSID.ssid_Length = LE32(len);
        CopyMem(ssid, &unit->wu_JoinParams.ej_SSID.ssid_Value, len);
    }

    /* Get BSSID or put broadcast BSSID */
    if ((ti = FindTagItem(S2INFO_BSSID, io->ios2_Data)))
    {
        CopyMem((APTR)ti->ti_Data, &unit->wu_JoinParams.ej_Assoc.ap_BSSID, 6);
    }
    else
    {
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[0] = 0xff;
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[1] = 0xff;
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[2] = 0xff;
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[3] = 0xff;
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[4] = 0xff;
        unit->wu_JoinParams.ej_Assoc.ap_BSSID[5] = 0xff;
    }

    /* TODO: Fill chan spec! */
    unit->wu_JoinParams.ej_Assoc.ap_ChanspecNum = 0;
    unit->wu_JoinParams.ej_Assoc.ap_ChanSpecList[0] = 0;

    ti = FindTagItem(S2INFO_WPAInfo, io->ios2_Data);
    if (ti != NULL && ti->ti_Data != 0)
    {
        UBYTE *info = (UBYTE*)ti->ti_Data;
        D(bug("[WiFi.0] WPAInfo at %08lx provided:", (ULONG)info));
        for (int i=0; i < info[1] + 2; i++)
        {
            D(bug(" %02lx", info[i]));
        }
        D(bug("\n"));

        ie_b = info;
        off = TLV_HDR_LEN;
        ie_found = 1;
    }

    unit->wu_JoinParams.ej_Scan.js_ScanYype = -1;
    unit->wu_JoinParams.ej_Scan.js_HomeTime = LE32(-1);
    unit->wu_JoinParams.ej_Scan.js_ActiveTime = LE32(-1);
    unit->wu_JoinParams.ej_Scan.js_PassiveTime = LE32(-1);
    unit->wu_JoinParams.ej_Scan.js_NProbes = LE32(-1);

    /* If IE was given parse it now */
    if (ie_found)
    {
        ULONG length = ie_b[1] + TLV_HDR_LEN;
        ULONG uniCount = 0;
        ULONG gval = 0;
        ULONG pval = 0;
        ULONG mfp = 0;
        ULONG wpa_auth = 0;
        int is_rsn_ie = !FindWPAIE(ie_b, length);

        D(bug("[WiFi.0] WPAInfo is %s\n", (ULONG)(is_rsn_ie ? "RSN":"WPA")));

        /* Skip header */
        if (!is_rsn_ie)
        {
            off += VS_IE_FIXED_HDR_LEN;
        }
        else
        {
            off += WPA_IE_VERSION_LEN;
        }

        if (off + WPA_IE_MIN_OUI_LEN > length)
        {
            D(bug("[WiFi.0] No multicast cipher suite\n"));
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
            io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
            return 1;
        }

        if (!brcmf_valid_wpa_oui(&ie_b[off], is_rsn_ie))
        {
            D(bug("[WiFi.0] Invalid OUI at offset %ld\n", off));
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
            io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
            return 1;
        }

        off += TLV_OUI_LEN;

        /* Select multicast cipher */
        switch (ie_b[off])
        {
            case WPA_CIPHER_NONE:
                gval = 0;
                D(bug("[WiFi.0] Multicast cipher: NONE\n"));
                break;
            
            case WPA_CIPHER_WEP_40: /* Fallthrough */
            case WPA_CIPHER_WEP_104:
                gval = WEP_ENABLED;
                D(bug("[WiFi.0] Multicast cipher: WEP\n"));
                break;
            
            case WPA_CIPHER_TKIP:
                gval = TKIP_ENABLED;
                D(bug("[WiFi.0] Multicast cipher: TKIP\n"));
                break;
            
            case WPA_CIPHER_AES_CCM:
                gval = AES_ENABLED;
                D(bug("[WiFi.0] Multicast cipher: AES\n"));
                break;

            default:
                D(bug("[WiFi.0] Invalid multicast cipher %ld\n", ie_b[off]));
                io->ios2_WireError = S2WERR_GENERIC_ERROR;
                io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
                return 1;
        }

        off++;
        
        /* Walk through unicast cipher list now and pick recognized one */
        uniCount = ie_b[off] + (ie_b[off + 1] << 8);
        off += WPA_IE_SUITE_COUNT_LEN;

        /* Sanity check - all unicast cipher TLVs must fit here */
        if (off + uniCount * WPA_IE_MIN_OUI_LEN > length)
        {
            D(bug("[WiFi.0] Unicast cipher list does not fit in WPAInfo!\n"));
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
            io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
            return 1;
        }

        /* Go through the list */
        for (ULONG i = 0; i < uniCount; i++)
        {
            if (!brcmf_valid_wpa_oui(&ie_b[off], is_rsn_ie))
            {
                D(bug("[WiFi.0] Invalid OUI at offset %ld\n", off));
                io->ios2_WireError = S2WERR_GENERIC_ERROR;
                io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
                return 1;
            }

            off += TLV_OUI_LEN;
            switch(ie_b[off])
            {
                case WPA_CIPHER_NONE:
                    break;
                case WPA_CIPHER_WEP_40: /* Fallthrough */
                case WPA_CIPHER_WEP_104:
                    D(bug("[WiFi.0] Unicast cipher: WEP\n"));
                    pval |= WEP_ENABLED;
                    break;
                case WPA_CIPHER_TKIP:
                    D(bug("[WiFi.0] Unicast cipher: TKIP\n"));
                    pval |= TKIP_ENABLED;
                    break;
                case WPA_CIPHER_AES_CCM:
                    D(bug("[WiFi.0] Unicast cipher: AES\n"));
                    pval |= AES_ENABLED;
                    break;
                default:
                    D(bug("[WiFi.0] Invalid multicast cipher %ld\n", ie_b[off]));
                    io->ios2_WireError = S2WERR_GENERIC_ERROR;
                    io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
                    return 1;
            }
            off++;
        }

        /* Walk through auth management list now and pick recognized one */
        uniCount = ie_b[off] + (ie_b[off + 1] << 8);
        off += WPA_IE_SUITE_COUNT_LEN;

        /* Sanity check - all unicast cipher TLVs must fit here */
        if (off + uniCount * WPA_IE_MIN_OUI_LEN > length)
        {
            D(bug("[WiFi.0] Auth management list does not fit in WPAInfo!\n"));
            io->ios2_WireError = S2WERR_GENERIC_ERROR;
            io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
            return 1;
        }

        /* Go through the list */
        for (ULONG i = 0; i < uniCount; i++)
        {
            if (!brcmf_valid_wpa_oui(&ie_b[off], is_rsn_ie))
            {
                D(bug("[WiFi.0] Invalid OUI at offset %ld\n", off));
                io->ios2_WireError = S2WERR_GENERIC_ERROR;
                io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
                return 1;
            }

            off += TLV_OUI_LEN;
            switch(ie_b[off])
            {
                case RSN_AKM_NONE:
                    D(bug("[WiFi.0] WPA_AUTH_NONE\n"));
                    wpa_auth |= WPA_AUTH_NONE;
                    break;
                
                case RSN_AKM_UNSPECIFIED:
                    D(bug("[WiFi.0] WPA%s_AUTH_UNSPECIFIED\n", (ULONG)(is_rsn_ie ? "2":"")));
                    wpa_auth |= is_rsn_ie ? WPA2_AUTH_UNSPECIFIED : WPA_AUTH_UNSPECIFIED;
                    break;
                
                case RSN_AKM_PSK:
                    D(bug("[WiFi.0] WPA%s_AUTH_PSK\n", (ULONG)(is_rsn_ie ? "2":"")));
                    wpa_auth |= is_rsn_ie ? WPA2_AUTH_PSK : WPA_AUTH_PSK;
                    break;

                case RSN_AKM_SHA256_PSK:
                    D(bug("[WiFi.0] WPA2_AUTH_MFP_PSK\n"));
                    wpa_auth |= WPA2_AUTH_PSK_SHA256;
                    break;

                case RSN_AKM_SHA256_1X:
                    D(bug("[WiFi.0] WPA2_AUTH_MFP_1X\n"));
                    wpa_auth |= WPA2_AUTH_1X_SHA256;
                    break;

                case RSN_AKM_SAE:
                    D(bug("[WiFi.0] WPA3_AUTH_SAE_PSK\n"));
                    wpa_auth |= WPA3_AUTH_SAE_PSK;
                    break;

                default:
                    D(bug("[WiFi.0] Invalid Auth management %ld\n", ie_b[off]));
                    io->ios2_WireError = S2WERR_GENERIC_ERROR;
                    io->ios2_Req.io_Error = S2ERR_NOT_SUPPORTED;
                    return 1;
            }
        }

        mfp = BRCMF_MFP_NONE;
        /* TODO: handle MFP... */

        /* Send WPA IE to WiFi module */
        PacketSetVar(WiFiBase->w_SDIO, "wpaie", ie_b, length);

        ULONG wsec = pval | gval; // | SES_OW_ENABLED;

        /* Set auth to OPEN - required by WPA/WPA2/WPA3 */
        PacketSetVarInt(WiFiBase->w_SDIO, "auth", 0);

        /* Set wsec */
        PacketSetVarInt(WiFiBase->w_SDIO, "wsec", wsec);
        
        /* Set MFP if set/required/enabled */
        PacketSetVarInt(WiFiBase->w_SDIO, "mfp", mfp);

        /* Set wpa auth now */
        PacketSetVarInt(WiFiBase->w_SDIO, "wpa_auth", wpa_auth);

        if (unit->wu_WPAInfo != NULL) FreeVecPooled(WiFiBase->w_MemPool, unit->wu_WPAInfo);
        unit->wu_WPAInfo = AllocVecPooled(WiFiBase->w_MemPool, ie_b[1] + 2);
        CopyMem(ie_b, unit->wu_WPAInfo, ie_b[1] + 2);
    }
    else
    {
        PacketSetVarInt(WiFiBase->w_SDIO, "auth", 0);
        PacketSetVarInt(WiFiBase->w_SDIO, "wsec", 0);
        PacketSetVarInt(WiFiBase->w_SDIO, "wpa_auth", WPA_AUTH_DISABLED);
        PacketSetVarInt(WiFiBase->w_SDIO, "mfp", 0);
    }

    PacketSetVar(WiFiBase->w_SDIO, "join", &unit->wu_JoinParams, sizeof(struct ExtJoinParams));

    return 1;
}

static int Do_S2_GETNETWORKINFO(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    APTR memPool = io->ios2_Data;
    struct TagItem *tags;

    D(bug("[WiFi.0] S2_GETNETWORKINFO\n"));

    if ((unit->wu_Flags & IFF_ONLINE) == 0)
    {
        io->ios2_WireError = S2WERR_UNIT_OFFLINE;
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        return 1;
    }
    else if ((unit->wu_Flags & IFF_CONNECTED) == 0)
    {
        io->ios2_WireError = S2WERR_NOT_CONFIGURED;
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        return 1;
    }

    io->ios2_StatData = tags = AllocPooled(memPool, sizeof(struct TagItem) * 4);
    if (io->ios2_StatData == NULL)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        return 1;
    }
    
    tags->ti_Tag = S2INFO_SSID;
    tags->ti_Data = (ULONG)AllocVecPooledClear(memPool, LE32(unit->wu_JoinParams.ej_SSID.ssid_Length) + 1);
    if (tags->ti_Data == 0)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        return 1;
    }
    CopyMem(unit->wu_JoinParams.ej_SSID.ssid_Value, (APTR)tags->ti_Data, LE32(unit->wu_JoinParams.ej_SSID.ssid_Length));
    tags++;

    tags->ti_Tag = S2INFO_BSSID;
    tags->ti_Data = (ULONG)AllocVecPooled(memPool, 6);
    if (tags->ti_Data == 0)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        return 1;
    }
    CopyMem(unit->wu_JoinParams.ej_Assoc.ap_BSSID, (APTR)tags->ti_Data, 6);
    tags++;

#if 0
    if (unit->wu_AssocIELength != 0)
    {
        APTR ie = FindWPAIE(unit->wu_AssocIE, unit->wu_AssocIELength);
        ULONG ieLen = 0;
        
        D(bug("[WiFi.0] AssocIELength = %ld\n", unit->wu_AssocIELength));

        if (ie)
        {
            ieLen = TLV_HDR_LEN + ((UBYTE*)ie)[1];
            D(bug("[WiFi.0] WPA IE at %08lx, size %ld\n", (ULONG)ie, ieLen));
        }
        else
        {
            ie = NULL;
            APTR rsn_ie = brcmf_parse_tlvs(unit->wu_AssocIE, unit->wu_AssocIELength, 48 /* WLAN_EID_RSN*/);
            if (rsn_ie)
            {
                ie = rsn_ie;
                ieLen = ((UBYTE*)ie)[1] + TLV_HDR_LEN;
                D(bug("[WiFi.0] RSN IE at %08lx, size %ld\n", (ULONG)ie, ieLen));
            }
        }

        if (ie && ieLen)
        {
            tags->ti_Tag = S2INFO_WPAInfo;
            tags->ti_Data = (ULONG)AllocVecPooled(memPool, ieLen);
            if (tags->ti_Data == 0)
            {
                io->ios2_WireError = S2WERR_BUFF_ERROR;
                io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
                return 1;
            }
            CopyMem(ie, (APTR)tags->ti_Data, ieLen);
            tags++;
        }
    }
#endif
    if (unit->wu_WPAInfo)
    {
        tags->ti_Tag = S2INFO_WPAInfo;
        tags->ti_Data = (ULONG)AllocPooled(memPool, unit->wu_WPAInfo[1] + 2);
        CopyMem(unit->wu_WPAInfo, (APTR)tags->ti_Data, unit->wu_WPAInfo[1] + 2);
        tags++;
    }
    

    tags->ti_Tag = TAG_DONE;
    tags->ti_Data = 0;
    io->ios2_Req.io_Error = 0;

    return 1;
}

static int Do_CMD_FLUSH(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;
    struct SDIO *sdio = unit->wu_Base->w_SDIO;
    struct IOSana2Req *req;
    struct Opener *opener;

    D(bug("[WiFi.0] CMD_FLUSH\n"));

    /* Flush and cancel all write requests */
    while ((req = (struct IOSana2Req *)GetMsg(sdio->s_SenderPort)))
    {
        req->ios2_Req.io_Error = IOERR_ABORTED;
        req->ios2_WireError = 0;
        ReplyMsg((struct Message *)req);
    }

    /* Flush network scan requests */
    Disable();
    if (unit->wu_ScanRequest != NULL)
    {
        req = unit->wu_ScanRequest;
        req->ios2_Req.io_Error = IOERR_ABORTED;
        req->ios2_WireError = 0;
        ReplyMsg((struct Message *)req);
        unit->wu_ScanRequest = NULL;
    }

    /* Flush scan queue */
    while ((req = (struct IOSana2Req *)GetMsg(unit->wu_ScanQueue)))
    {
        req->ios2_Req.io_Error = IOERR_ABORTED;
        req->ios2_WireError = 0;
        ReplyMsg((struct Message *)req);
    }
    Enable();

    /* For every opener, flush orphan and even queues */
    ForeachNode(&unit->wu_Openers, opener)
    {
        while ((req = (struct IOSana2Req *)GetMsg(&opener->o_OrphanListeners)))
        {
            req->ios2_Req.io_Error = IOERR_ABORTED;
            req->ios2_WireError = 0;
            ReplyMsg((struct Message *)req);
        }

        while ((req = (struct IOSana2Req *)GetMsg(&opener->o_EventListeners)))
        {
            req->ios2_Req.io_Error = IOERR_ABORTED;
            req->ios2_WireError = 0;
            ReplyMsg((struct Message *)req);
        }
    }

    return 1;
}

static int Do_NSCMD_DEVICEQUERY(struct IOStdReq *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    struct NSDeviceQueryResult *dq;
    dq = io->io_Data;

    D(bug("[WiFi.0] NSCMD_DEVICEQUERY\n"));
    if (dq == NULL)
    {
        io->io_Error = IOERR_BADADDRESS;
    }
    else
    {
        /* Fill out structure */
        dq->nsdqr_DevQueryFormat = 0;
        dq->nsdqr_DeviceType = NSDEVTYPE_SANA2;
        dq->nsdqr_DeviceSubType = 0;
        dq->nsdqr_SupportedCommands = (UWORD*)WiFi_SupportedCommands;
        io->io_Actual = sizeof(struct NSDeviceQueryResult);
        dq->nsdqr_SizeAvailable = io->io_Actual;
        io->io_Error = 0;
    }
    return 1;
}

static int Do_CMD_READ(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    // If interface is up, put the read request in units read queue
    if (unit->wu_Flags & IFF_UP)
    {
        struct Opener *opener = io->ios2_BufferManagement;
        io->ios2_Req.io_Flags &= ~IOF_QUICK;
        PutMsg(&opener->o_ReadPort, (struct Message *)io);
        return 0;
    }
    else
    {
        io->ios2_WireError = S2WERR_UNIT_OFFLINE;
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        return 1;
    }
}

static int Do_S2_READORPHAN(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;

    D(bug("[WiFi.0] CMD_READORPHAN\n"));

    // If interface is up, put the read request in units read queue
    if (unit->wu_Flags & IFF_UP)
    {
        struct Opener *opener = io->ios2_BufferManagement;
        io->ios2_Req.io_Flags &= ~IOF_QUICK;
        PutMsg(&opener->o_OrphanListeners, (struct Message *)io);
        return 0;
    }
    else
    {
        io->ios2_WireError = S2WERR_UNIT_OFFLINE;
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        return 1;
    }
}

static int Do_CMD_WRITE(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct ExecBase *SysBase = unit->wu_Base->w_SysBase;
    struct SDIO *sdio = unit->wu_Base->w_SDIO;

    if (unit->wu_Flags & IFF_UP)
    {
        io->ios2_Req.io_Flags &= ~IOF_QUICK;
        PutMsg(sdio->s_SenderPort, (struct Message *)io);
        return 0;
    }
    else
    {
        io->ios2_WireError = S2WERR_UNIT_OFFLINE;
        io->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        return 1;
    }
}

static void UpdateMCastList(struct WiFiUnit *unit)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct SDIO *sdio = WiFiBase->w_SDIO;

    ULONG totalCount = 0;
    struct MulticastRange *range;

    /* Count number of multicast addresses */
    ForeachNode(&unit->wu_MulticastRanges, range)
    {
        LONG cnt = range->mr_UpperBound - range->mr_LowerBound;
        if (cnt < 0) cnt = -cnt;

        totalCount += cnt + 1;
    }

    UBYTE *list = AllocVecPooled(WiFiBase->w_MemPool, totalCount * 6 + 4);
    UBYTE *dst = list + 4;

    /* Put number of entries in the list first */
    *(ULONG*)list = LE32(totalCount);

    ForeachNode(&unit->wu_MulticastRanges, range)
    {
        union {
            uint64_t    u64;
            uint8_t     u8[8];
            uint16_t    u16[4];
            uint32_t    u32[2];
        } u;

        for (u.u64 = range->mr_LowerBound; u.u64 <= range->mr_UpperBound; u.u64++)
        {
            *(UWORD*)dst = u.u16[1]; dst+=2;
            *(ULONG*)dst = u.u32[1]; dst+=4;
        }
    }

    PacketSetVar(sdio, "mcast_list", list, totalCount * 6 + 4);

    FreeVecPooled(WiFiBase->w_MemPool, list);
}

static int Do_S2_ADDMULTICASTADDRESSES(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;

    D(bug("[WiFi.0] S2_ADDMULTICASTADDRESS%s\n", (ULONG)(io->ios2_Req.io_Command == S2_ADDMULTICASTADDRESSES ? "ES":"")));

    struct MulticastRange *range;
    union {
        uint64_t u64;
        uint8_t u8[8];
    } u;

    u.u8[0] = u.u8[1] = 0;
    uint64_t lower_bound, upper_bound;
    u.u8[2] = io->ios2_SrcAddr[0];
    u.u8[3] = io->ios2_SrcAddr[1];
    u.u8[4] = io->ios2_SrcAddr[2];
    u.u8[5] = io->ios2_SrcAddr[3];
    u.u8[6] = io->ios2_SrcAddr[4];
    u.u8[7] = io->ios2_SrcAddr[5];
    lower_bound = u.u64;
    if (io->ios2_Req.io_Command == S2_ADDMULTICASTADDRESS)
    {
        upper_bound = lower_bound;
    }
    else
    {
        u.u8[2] = io->ios2_DstAddr[0];
        u.u8[3] = io->ios2_DstAddr[1];
        u.u8[4] = io->ios2_DstAddr[2];
        u.u8[5] = io->ios2_DstAddr[3];
        u.u8[6] = io->ios2_DstAddr[4];
        u.u8[7] = io->ios2_DstAddr[5];
        upper_bound = u.u64;
    }

    for (uint64_t mac = lower_bound; mac <= upper_bound; mac++)
    {
        union {
            uint64_t u64;
            uint8_t u8[8];
        } u;
        u.u64 = mac;

        D(bug("[WiFi.0] * "));
        D(bug("%02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n", 
                u.u8[2], u.u8[3], u.u8[4], u.u8[5], u.u8[6], u.u8[7]));
    }

    /* Go through already registered multicast ranges. If one is found, increase use count and return */
    ForeachNode(&unit->wu_MulticastRanges, range)
    {
        if (range->mr_LowerBound == lower_bound && range->mr_UpperBound == upper_bound)
        {
            range->mr_UseCount++;
            return 1;
        }
    }

    /* No range was found. Create new one and add the multicast range on the WiFi module */
    range = AllocPooledClear(WiFiBase->w_MemPool, sizeof(struct MulticastRange));
    range->mr_UseCount = 1;
    range->mr_LowerBound = lower_bound;
    range->mr_UpperBound = upper_bound;
    AddHead((struct List *)&unit->wu_MulticastRanges, (struct Node *)range);

    /* Add range on WiFi now */
    UpdateMCastList(unit);

    return 1;
}

static int Do_S2_DELMULTICASTADDRESSES(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;

    D(bug("[WiFi.0] S2_DELMULTICASTADDRESS%s\n", (ULONG)(io->ios2_Req.io_Command == S2_DELMULTICASTADDRESSES ? "ES":"")));

    union {
        uint64_t u64;
        uint8_t u8[8];
    } u;

    u.u8[0] = u.u8[1] = 0;

    struct MulticastRange *range;
    uint64_t lower_bound, upper_bound;
    u.u8[2] = io->ios2_SrcAddr[0];
    u.u8[3] = io->ios2_SrcAddr[1];
    u.u8[4] = io->ios2_SrcAddr[2];
    u.u8[5] = io->ios2_SrcAddr[3];
    u.u8[6] = io->ios2_SrcAddr[4];
    u.u8[7] = io->ios2_SrcAddr[5];
    lower_bound = u.u64;
    if (io->ios2_Req.io_Command == S2_ADDMULTICASTADDRESS)
    {
        upper_bound = lower_bound;
    }
    else
    {
        u.u8[2] = io->ios2_DstAddr[0];
        u.u8[3] = io->ios2_DstAddr[1];
        u.u8[4] = io->ios2_DstAddr[2];
        u.u8[5] = io->ios2_DstAddr[3];
        u.u8[6] = io->ios2_DstAddr[4];
        u.u8[7] = io->ios2_DstAddr[5];
        upper_bound = u.u64;
    }

    /* Go through already registered multicast ranges. Once found, decrease use count */
    ForeachNode(&unit->wu_MulticastRanges, range)
    {
        if (range->mr_LowerBound == lower_bound && range->mr_UpperBound == upper_bound)
        {
            range->mr_UseCount--;

            /* No user of this multicast range. Remove it and unregister on WiFi module */
            if (range->mr_UseCount == 0)
            {
                Remove((struct Node *)range);
                FreePooled(WiFiBase->w_MemPool, range, sizeof(struct MulticastRange));

                /* Remove the range on WiFi now... */
                UpdateMCastList(unit);
            }
            return 1;
        }
    }

    return 1;
}

static int Do_S2_GETNETWORKS(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;

    /* GetNetworks is never quick */
    io->ios2_Req.io_Flags &= ~IOF_QUICK;
    
    D(bug("[WiFi.0] S2_GETNETWORKS\n"));

    /* Put it into scan queue */
    PutMsg(unit->wu_ScanQueue, (struct Message *)io);

    return 0;
}

int Do_S2_DEVICEQUERY(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct Sana2DeviceQuery *info = io->ios2_StatData;
    ULONG size;

    D(bug("[WiFi.0] S2_DEVICEQUERY\n"));

    size = info->SizeAvailable;
    if (size < sizeof(*info))
    {
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        io->ios2_WireError = S2WERR_GENERIC_ERROR;
        
        return 1;
    }
    info->AddrFieldSize = 48;
    info->HardwareType = S2WireType_Ethernet;
    info->BPS = 100000000;
    info->MTU = 1500;
    info->SizeAvailable = size;
    info->SizeSupplied = sizeof(struct Sana2DeviceQuery);
    return 1;
}

static int Do_S2_ONLINE(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct TimerBase *TimerBase = unit->wu_TimerBase;

    D(bug("[WiFi.0] S2_ONLINE\n"));

    // Bring all stats to 0
    _bzero(&unit->wu_Stats, sizeof(struct Sana2DeviceStats));
    
    // Get last start time
    GetSysTime(&unit->wu_Stats.LastStart);

    /* If unit was not yet online, report event now */

    D(bug("[WiFi.0] unit->wu_Flags = %08lx\n", unit->wu_Flags));

    if ((unit->wu_Flags & IFF_ONLINE) == 0)
    {
        unit->wu_Flags |= IFF_ONLINE;

        ReportEvents(unit, S2EVENT_ONLINE);
    }

    return 1;
}

static int Do_S2_CONFIGINTERFACE(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct SDIO *sdio = WiFiBase->w_SDIO;

    D(bug("[WiFi.0] S2_CONFIGINTERFACE\n"));

    if (unit->wu_Flags & IFF_CONFIGURED)
    {
        io->ios2_Req.io_Error = S2ERR_BAD_STATE;
        io->ios2_WireError = S2WERR_IS_CONFIGURED;
    }
    else
    {
        /* Try to set HW addr */
        PacketSetVar(sdio, "cur_etheraddr", io->ios2_SrcAddr, 6);

        /* Get HW addr back */
        PacketGetVar(sdio, "cur_etheraddr", unit->wu_EtherAddr, 6);
        D(bug("[WiFi.0] Ethernet addr set: %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
                    unit->wu_EtherAddr[0], unit->wu_EtherAddr[1], unit->wu_EtherAddr[2],
                    unit->wu_EtherAddr[3], unit->wu_EtherAddr[4], unit->wu_EtherAddr[5]));

        ULONG d11Type = 0;
        static const char * const types[]= { "UNKNOWN", "N", "AC" };
        if (0 == PacketCmdIntGet(sdio, BRCMF_C_GET_VERSION, &d11Type))
        {
            D(bug("[WiFi] D11 Version: %s\n", (ULONG)types[d11Type]));
            sdio->s_Chip->c_D11Type = d11Type;
        }

        PacketUploadCLM(sdio);

        PacketSetVarInt(sdio, "assoc_listen", 10);

        struct JoinPrefParams jpp[2];
        jpp[0].jp_Type = JOIN_PREF_RSSI_DELTA;
        jpp[0].jp_Len = 2;
        jpp[0].jp_RSSIGain = 8;
        jpp[0].jp_Band = 1; // 1 - 5GHz, 2 - 2.4GHz, 3 - all, 0 - Auto select

        jpp[1].jp_Type = JOIN_PREF_RSSI;
        jpp[1].jp_Len = 0;
        jpp[1].jp_RSSIGain = 0;
        jpp[1].jp_Band = 0;

        PacketSetVar(sdio, "join_pref", jpp, sizeof(jpp));

        if (sdio->s_Chip->c_ChipID == BRCM_CC_43430_CHIP_ID || sdio->s_Chip->c_ChipID == BRCM_CC_4345_CHIP_ID)
        {
            PacketCmdInt(sdio, 0x56, 0);
        }
        else
        {
            PacketCmdInt(sdio, 0x56, 2);
        }

        PacketSetVarInt(sdio, "bus:txglom", 1);
        PacketSetVarInt(sdio, "bus:txglomalign", 4);
        if (PacketSetVarInt(sdio, "bus:rxglom", 1) == 0)
        sdio->s_GlomEnabled = TRUE;
        PacketSetVarInt(sdio, "bcn_timeout", 10);
        PacketSetVarInt(sdio, "assoc_retry_max", 3);

        /* Pepare event mask. Allow only events which are really needed */
        UBYTE ev_mask[(BRCMF_E_LAST + 7) / 8];
        for (int i=0; i < (BRCMF_E_LAST + 7) / 8; i++) ev_mask[i] = 0;

#define EVENT_BIT(mask, i) (mask)[(i) / 8] |= 1 << ((i) % 8)
#define EVENT_BIT_CLEAR(mask, i) (mask)[(i) / 8] &= ~(1 << ((i) % 8))
        EVENT_BIT(ev_mask, BRCMF_E_IF);
        EVENT_BIT(ev_mask, BRCMF_E_LINK);
        EVENT_BIT(ev_mask, BRCMF_E_AUTH);
        EVENT_BIT(ev_mask, BRCMF_E_ASSOC);
        EVENT_BIT(ev_mask, BRCMF_E_DEAUTH);
        EVENT_BIT(ev_mask, BRCMF_E_DISASSOC);
        EVENT_BIT(ev_mask, BRCMF_E_REASSOC);
        EVENT_BIT(ev_mask, BRCMF_E_ESCAN_RESULT);
        EVENT_BIT_CLEAR(ev_mask, 124);
#undef EVENT_BIT

        PacketSetVar(sdio, "event_msgs", ev_mask, (BRCMF_E_LAST + 7) / 8);

        PacketCmdInt(sdio, BRCMF_C_SET_SCAN_CHANNEL_TIME, 40);
        PacketCmdInt(sdio, BRCMF_C_SET_SCAN_UNASSOC_TIME, 40);
        PacketCmdInt(sdio, BRCMF_C_SET_SCAN_PASSIVE_TIME, 120);

        PacketCmdInt(sdio, BRCMF_C_UP, 0);

        char ver[128];
        for (int i=0; i < 128; i++) ver[i] = 0;
        PacketGetVar(sdio, "ver", ver, 128);

        // Remove \r and \n from version string. Replace first found with 0
        for (int i=0; i < 128; i++) { if (ver[i] == 13 || ver[i] == 10) { ver[i] = 0; break; } }
        D(bug("[WiFi.0] Firmware version: %s\n", (ULONG)ver));

        PacketSetVarInt(sdio, "roam_off", 1);

        /* Enable TX beamforming */
        //PacketSetVarInt(sdio, "txbf", 1);

#if 0
        /* Disable all offloading (ARP, NDP, TCP/UDP cksum). */
        PacketSetVarInt(sdio, "arp_ol", 0);
        PacketSetVarInt(sdio, "arpoe", 0);
        PacketSetVarInt(sdio, "ndoe", 0);
        PacketSetVarInt(sdio, "toe", 0);
#endif

        PacketSetVarInt(sdio, "sup_wpa", 0);

        PacketCmdInt(sdio, BRCMF_C_SET_INFRA, 1);
        PacketCmdInt(sdio, BRCMF_C_SET_AP, 0);
        PacketCmdInt(sdio, BRCMF_C_SET_PROMISC, 0);
        PacketCmdInt(sdio, BRCMF_C_UP, 1);

        // If Network Config is already set up, attempt to connect.
        // For now, only open networks are supported
        if (WiFiBase->w_NetworkConfig.nc_Open && WiFiBase->w_NetworkConfig.nc_SSID)
        {
            struct WiFiNetwork *network = AllocVecPooledClear(WiFiBase->w_MemPool, sizeof(struct WiFiNetwork));
            network->wn_SSIDLength = _strlen(WiFiBase->w_NetworkConfig.nc_SSID);
            CopyMem(WiFiBase->w_NetworkConfig.nc_SSID, network->wn_SSID, network->wn_SSIDLength);
            for (int i=0; i < 6; i++) network->wn_BSID[i] = 0xff;
            
            //Connect(sdio, network);

            FreeVecPooled(WiFiBase->w_MemPool, network);
        }

        D(bug("[WiFi.0] Interface is up\n"));

        /* Get current address */
        CopyMem(unit->wu_EtherAddr, io->ios2_SrcAddr, 6);

        /* We do not allow to change ethernet address yet */
        unit->wu_Flags |= IFF_CONFIGURED | IFF_UP;

        /* Bring unit online */
        Do_S2_ONLINE(io);
    }

    return 1;
}

static int Do_S2_OFFLINE(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct SDIO *sdio = WiFiBase->w_SDIO;
    struct IOSana2Req *req;

    D(bug("[WiFi.0] S2_OFFLINE\n"));

    /* Flush network scan requests */
    Disable();
    if (unit->wu_ScanRequest != NULL)
    {
        req = unit->wu_ScanRequest;
        req->ios2_Req.io_Error = IOERR_ABORTED;
        req->ios2_WireError = 0;
        ReplyMsg((struct Message *)req);
        unit->wu_ScanRequest = NULL;
    }

    /* Flush scan queue */
    while ((req = (struct IOSana2Req *)GetMsg(unit->wu_ScanQueue)))
    {
        req->ios2_Req.io_Error = IOERR_ABORTED;
        req->ios2_WireError = 0;
        ReplyMsg((struct Message *)req);
    }
    Enable();

    /* Flush and cancel all write requests */
    while ((req = (struct IOSana2Req *)GetMsg(sdio->s_SenderPort)))
    {
        req->ios2_Req.io_Error = S2ERR_OUTOFSERVICE;
        req->ios2_WireError = S2WERR_UNIT_OFFLINE;
        ReplyMsg((struct Message *)req);
    }

    /* If unit was ONLINE before, report offline event now */
    if (unit->wu_Flags & IFF_ONLINE)
    {
        unit->wu_Flags &= ~IFF_ONLINE;

        ReportEvents(unit, S2EVENT_OFFLINE);
    }

    return 1;
}

void HandleRequest(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;

    ULONG complete = 0;

    /*
        Only NSCMD_DEVICEQUERY can use standard sized request. All other must be of 
        size IOSana2Req
    */
    if (io->ios2_Req.io_Message.mn_Length < sizeof(struct IOSana2Req) &&
        io->ios2_Req.io_Command != NSCMD_DEVICEQUERY)
    {
        io->ios2_Req.io_Error = IOERR_BADLENGTH;
        complete = 1;
    }
    else
    {
        io->ios2_Req.io_Error = 0;

        switch (io->ios2_Req.io_Command)
        {
            case NSCMD_DEVICEQUERY:
                complete = Do_NSCMD_DEVICEQUERY((struct IOStdReq *)io);
                break;

            case S2_DEVICEQUERY:
                complete = Do_S2_DEVICEQUERY(io);
                break;
        
            case S2_GETSTATIONADDRESS:
                D(bug("[WiFi.0] S2_GETSTATIONADDRESS\n"));
                CopyMem(unit->wu_OrigEtherAddr, io->ios2_DstAddr, 6);
                CopyMem(unit->wu_EtherAddr, io->ios2_SrcAddr, 6);
                io->ios2_Req.io_Error = 0;
                complete = 1;
                break;

            case S2_GETGLOBALSTATS:
                D(bug("[WiFi.0] S2_GETGLOBALSTATS\n"));
                CopyMem(&unit->wu_Stats, io->ios2_StatData, sizeof(struct Sana2DeviceStats));
                io->ios2_Req.io_Error = 0;
                complete = 1;
                break;

            case S2_GETNETWORKS:
                complete = Do_S2_GETNETWORKS(io);
                break;

            case S2_ADDMULTICASTADDRESS: /* Fallthrough */
            case S2_ADDMULTICASTADDRESSES:
                complete = Do_S2_ADDMULTICASTADDRESSES(io);
                break;

            case S2_DELMULTICASTADDRESS: /* Fallthrough */
            case S2_DELMULTICASTADDRESSES:
                complete = Do_S2_DELMULTICASTADDRESSES(io);
                break;
            
            case S2_CONFIGINTERFACE:
                complete = Do_S2_CONFIGINTERFACE(io);
                break;
            
            case S2_ONLINE:
                complete = Do_S2_ONLINE(io);
                break;

            case S2_OFFLINE:
                complete = Do_S2_OFFLINE(io);
                break;

            case CMD_READ:
                complete = Do_CMD_READ(io);
                break;

            case CMD_FLUSH:
                complete = Do_CMD_FLUSH(io);
                break;
            
            case S2_READORPHAN:
                complete = Do_S2_READORPHAN(io);
                break;

            case S2_ONEVENT:
                complete = Do_S2_ONEVENT(io);
                break;

            case S2_GETSIGNALQUALITY:
                complete = Do_S2_GETSIGNALQUALITY(io);
                break;
            
            case S2_SETKEY:
                complete = Do_S2_SETKEY(io);
                break;

            case S2_SETOPTIONS:
                complete = Do_S2_SETOPTIONS(io);
                break;

            case S2_GETNETWORKINFO:
                complete = Do_S2_GETNETWORKINFO(io);
                break;
            
            case S2_GETCRYPTTYPES:
                complete = Do_S2_GETCRYPTTYPES(io);
                break;

            case S2_BROADCAST: /* Fallthrough */
                io->ios2_DstAddr[0] = 0xff;
                io->ios2_DstAddr[1] = 0xff;
                io->ios2_DstAddr[2] = 0xff;
                io->ios2_DstAddr[3] = 0xff;
                io->ios2_DstAddr[4] = 0xff;
                io->ios2_DstAddr[5] = 0xff;
            case S2_MULTICAST: /* Fallthrough */
            case CMD_WRITE:
                complete = Do_CMD_WRITE(io);
                break;

            default:
                D(bug("[WiFi.0] Unknown command %ld\n", io->ios2_Req.io_Command));
                io->ios2_Req.io_Error = IOERR_NOCMD;
                complete = 1;
                break;
        }
    }

    // If command is complete and not quick, reply it now
    if (complete && !(io->ios2_Req.io_Flags & IOF_QUICK))
    {
        ReplyMsg((struct Message *)io);
    }
}
