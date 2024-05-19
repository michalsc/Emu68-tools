#ifndef _WIFIPI_H
#define _WIFIPI_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <common/compiler.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <libraries/dos.h>
#include <intuition/intuitionbase.h>
#include <libraries/expansionbase.h>

#include <stdint.h>

#include "sdio.h"
#include "d11.h"
#include "packet.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define WIFIPI_VERSION  0
#define WIFIPI_REVISION 1
#define WIFIPI_PRIORITY -100

struct WiFiUnit;

struct NetworkConfig {
    UBYTE *     nc_SSID;
    UBYTE *     nc_PSK;
    UBYTE       nc_Open;
};

struct WiFiBase
{
    struct Device       w_Device;
    BPTR                w_SegList;
    struct ExecBase *   w_SysBase;
    struct Library *    w_UtilityBase;
    struct WiFiUnit *   w_Unit;
    struct Library *    w_DosBase;
    APTR                w_DeviceTreeBase;
    APTR                w_SDIOBase;
    APTR                w_MailBox;
    APTR                w_GPIOBase;
    APTR                w_RequestOrig;
    APTR                w_MemPool;
    ULONG *             w_Request;
    ULONG               w_SDIOClock;
    struct SDIO *       w_SDIO;
    UBYTE *             w_NetworkConfigVar;
    ULONG               w_NetworkConfigLength;
    struct NetworkConfig    w_NetworkConfig;
};

struct WiFiNetwork {
    struct MinNode      wn_Node;
    UBYTE               wn_BSID[6];         // MAC of broadcast station
    WORD                wn_RSSI;            // Relative signal strength
    UBYTE               wn_SSIDLength;      // Length of network SSID
    UBYTE               wn_SSID[33];        // SSID
    UBYTE               wn_LastUpdated;     // Cleared on update, increased of not updated
    UWORD               wn_BeaconPeriod;
    struct ChannelInfo  wn_ChannelInfo;     // Channel spec and info
    ULONG               wn_IELength;
    UBYTE *             wn_IE;
};

struct Chip;

struct Core {
    struct MinNode      c_Node;
    struct Chip *       c_Chip;
    ULONG               c_BaseAddress;
    ULONG               c_WrapBase;
    UWORD               c_CoreID;
    UBYTE               c_CoreREV;
};

struct Chip {
    struct WiFiBase     *c_WiFiBase;
    struct SDIO         *c_SDIO;

    UWORD               c_ChipID;
    UWORD               c_ChipREV;

    ULONG               c_Caps;
    ULONG               c_CapsExt;
    ULONG               c_PMUCaps;
    ULONG               c_PMURev;

    APTR                c_FirmwareBase;
    ULONG               c_FirmwareSize;

    APTR                c_CLMBase;
    ULONG               c_CLMSize;

    APTR                c_ConfigBase;
    ULONG               c_ConfigSize;

    ULONG               c_RAMBase;
    ULONG               c_RAMSize;
    ULONG               c_SRSize;

    struct MinList      c_Cores;

    UBYTE               c_D11Type;

    struct Core *       (*GetCore)(struct Chip *chip, UWORD coreID);
    void                (*SetPassive)(struct Chip *);
    BOOL                (*SetActive)(struct Chip *, ULONG resetVector);
    BOOL                (*IsCoreUp)(struct Chip *, struct Core *);
    void                (*DisableCore)(struct Chip *, struct Core *, ULONG preReset, ULONG reset);
    void                (*ResetCore)(struct Chip *, struct Core *, ULONG preReset, ULONG reset, ULONG postReset);
};

#define ForeachNode(list, node)                        \
for                                                    \
(                                                      \
    node = (void *)(((struct MinList *)(list))->mlh_Head); \
    ((struct MinNode *)(node))->mln_Succ;                  \
    node = (void *)(((struct MinNode *)(node))->mln_Succ)  \
)

#define ForeachNodeSafe(list, node, next)              \
for                                                       \
(                                                         \
    *(void **)&node = (void *)(((struct MinList *)(list))->mlh_Head); \
    (*(void **)&next = (void *)((struct MinNode *)(node))->mln_Succ); \
    *(void **)&node = (void *)next                                \
)

struct Key
{
    ULONG k_Type;
    ULONG k_Length;
    APTR  k_Key;
    ULONG k_RXCountHigh;
    UWORD k_RXCountLow;
};

struct WiFiUnit
{
    struct Unit             wu_Unit;
    struct MinList          wu_Openers;
    struct MinList          wu_MulticastRanges;
    struct MinList          wu_TypeTrackers;
    struct WiFiBase *       wu_Base;
    struct Task *           wu_Task;
    struct SignalSemaphore  wu_Lock;
    struct MsgPort *        wu_CmdQueue;
    struct MsgPort *        wu_ScanQueue;
    struct IOSana2Req *     wu_ScanRequest;
    struct Sana2DeviceStats wu_Stats;
    struct TimerBase *      wu_TimerBase;
    ULONG                   wu_Flags;
    UBYTE                   wu_OrigEtherAddr[6];
    UBYTE                   wu_EtherAddr[6];

    struct timeval          wu_GlomCreationTime;
    ULONG                   wu_GlomCount;
    UBYTE *                 wu_GlomLastItemMarker;

    struct Key              wu_Keys[4];
    struct ExtJoinParams    wu_JoinParams;
    UBYTE *                 wu_AssocIE;
    UWORD                   wu_AssocIELength;
    UBYTE *                 wu_WPAInfo;
};

struct MulticastRange {
    struct MinNode  mr_Node;
    ULONG           mr_UseCount;
    uint64_t        mr_LowerBound;
    uint64_t        mr_UpperBound;
};

struct Opener
{
    struct MinNode      o_Node;
    struct MsgPort      o_ReadPort;
    struct MsgPort      o_OrphanListeners;
    struct MsgPort      o_EventListeners;
    struct Hook *       o_FilterHook;
    BOOL              (*o_RXFunc)(REGARG(APTR, "a0"), REGARG(APTR, "a1"), REGARG(ULONG, "d0"));
    BOOL              (*o_TXFunc)(REGARG(APTR, "a0"), REGARG(APTR, "a1"), REGARG(ULONG, "d0"));
};

/* Standard interface flags (netdevice->flags). */
#define IFF_UP          0x1             /* interface is up              */
#define IFF_BROADCAST   0x2             /* broadcast address valid      */
#define IFF_DEBUG       0x4             /* turn on debugging            */
#define IFF_LOOPBACK    0x8             /* is a loopback net            */
#define IFF_POINTOPOINT 0x10            /* interface is has p-p link    */
#define IFF_NOTRAILERS  0x20            /* avoid use of trailers        */
#define IFF_RUNNING     0x40            /* resources allocated          */
#define IFF_NOARP       0x80            /* no ARP protocol              */
#define IFF_PROMISC     0x100           /* receive all packets          */
#define IFF_ALLMULTI    0x200           /* receive all multicast packets*/

#define IFF_MASTER      0x400           /* master of a load balancer    */
#define IFF_SLAVE       0x800           /* slave of a load balancer     */

#define IFF_MULTICAST   0x1000          /* Supports multicast           */

#define IFF_VOLATILE    (IFF_LOOPBACK|IFF_POINTOPOINT|IFF_BROADCAST|IFF_MASTER|IFF_SLAVE|IFF_RUNNING)

#define IFF_PORTSEL     0x002000         /* can set media type           */
#define IFF_AUTOMEDIA   0x004000         /* auto media select active     */
#define IFF_DYNAMIC     0x008000         /* dialup device with changing addresses*/
#define IFF_SHARED      0x010000         /* interface may be shared */
#define IFF_CONFIGURED  0x020000         /* interface already configured */
#define IFF_STARTED     0x040000         /* interface already started */
#define IFF_ONLINE      0x080000         /* interface online */
#define IFF_CONNECTED   0x100000         /* interface connected to network */

static inline __attribute__((always_inline)) void putch(REGARG(UBYTE data, "d0"), REGARG(APTR ignore, "a3"))
{
    (void)ignore;
    if (data != 0)
        *(UBYTE*)0xdeadbeef = data;
}


static inline uint64_t LE64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint32_t LE32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t LE16(uint16_t x) { return __builtin_bswap16(x); }

static inline ULONG rd32(APTR addr, ULONG offset)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    ULONG val = LE32(*(volatile ULONG *)addr_off);
    asm volatile("nop");
    return val;
}

static inline void wr32(APTR addr, ULONG offset, ULONG val)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    *(volatile ULONG *)addr_off = LE32(val);
    asm volatile("nop");
}

static inline ULONG rd32be(APTR addr, ULONG offset)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    ULONG val = *(volatile ULONG *)addr_off;
    asm volatile("nop");
    return val;
}

static inline void wr32be(APTR addr, ULONG offset, ULONG val)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    *(volatile ULONG *)addr_off = val;
    asm volatile("nop");
}

struct WiFiBase * WiFi_Init(REGARG(struct WiFiBase *base, "d0"), REGARG(BPTR seglist, "a0"),
                            REGARG(struct ExecBase *SysBase, "a6"));

#define bug(string, ...) \
    do { ULONG args[] = {0, __VA_ARGS__}; RawDoFmt((CONST_STRPTR)string, &args[1], (APTR)putch, NULL); } while(0)


BOOL LoadFirmware(struct Chip *chip);

int chip_init(struct SDIO *sdio);

void _bzero(APTR ptr, ULONG sz);
APTR _memcpy(APTR dst, CONST_APTR src, ULONG sz);
ULONG _strlen(CONST_STRPTR c);
STRPTR _strncpy(STRPTR dst, CONST_STRPTR src, ULONG len);
STRPTR _strcpy(STRPTR dst, CONST_STRPTR src);
int _strcmp(CONST_STRPTR s1, CONST_STRPTR s2);
int _strncmp(CONST_STRPTR s1, CONST_STRPTR s2, ULONG n);
void StartUnit(struct WiFiUnit *unit);
void StartUnitTask(struct WiFiUnit *unit);
void HandleRequest(struct IOSana2Req *io);
APTR AllocPooledClear(APTR pool, ULONG byteSize);
APTR AllocVecPooledClear(APTR pool, ULONG byteSize);
APTR AllocVecPooled(APTR pool, ULONG byteSize);
void FreeVecPooled(APTR pool, APTR buf);
void ProcessDataPacket(struct SDIO *, UBYTE *, ULONG);
void ParseConfig(struct WiFiBase *WiFiBase);
void ReportEvents(struct WiFiUnit *unit, ULONG eventSet);

#endif
