#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/alerts.h>
#include <devices/timer.h>
#include <devices/sana2wireless.h>

#if defined(__INTELLISENSE__)
#include <clib/exec_protos.h>
#include <clib/utility_protos.h>
#include <clib/timer_protos.h>
#else
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/timer.h>
#endif

#include "d11.h"
#include "sdio.h"
#include "brcm.h"
#include "wifipi.h"
#include "packet.h"
#include "brcm_wifi.h"

#ifndef	PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

/*
 * brcmfmac sdio bus specific header
 * This is the lowest layer header wrapped on the packets transmitted between
 * host and WiFi dongle which contains information needed for SDIO core and
 * firmware
 *
 * It consists of 3 parts: hardware header, hardware extension header and
 * software header
 * hardware header (frame tag) - 4 bytes
 * Byte 0~1: Frame length
 * Byte 2~3: Checksum, bit-wise inverse of frame length
 * hardware extension header - 8 bytes
 * Tx glom mode only, N/A for Rx or normal Tx
 * Byte 0~1: Packet length excluding hw frame tag
 * Byte 2: Reserved
 * Byte 3: Frame flags, bit 0: last frame indication
 * Byte 4~5: Reserved
 * Byte 6~7: Tail padding length
 * software header - 8 bytes
 * Byte 0: Rx/Tx sequence number
 * Byte 1: 4 MSB Channel number, 4 LSB arbitrary flag
 * Byte 2: Length of next data frame, reserved for Tx
 * Byte 3: Data offset
 * Byte 4: Flow control bits, reserved for Tx
 * Byte 5: Maximum Sequence number allowed by firmware for Tx, N/A for Rx packet
 * Byte 6~7: Reserved
 */
#define SDPCM_HWHDR_LEN			4
#define SDPCM_HWEXT_LEN			8
#define SDPCM_SWHDR_LEN			8
#define SDPCM_HDRLEN			(SDPCM_HWHDR_LEN + SDPCM_SWHDR_LEN)
/* software header */
#define SDPCM_SEQ_MASK			0x000000ff
#define SDPCM_SEQ_WRAP			256
#define SDPCM_CHANNEL_MASK		0x00000f00
#define SDPCM_CHANNEL_SHIFT		8
#define SDPCM_CONTROL_CHANNEL	0	/* Control */
#define SDPCM_EVENT_CHANNEL		1	/* Asyc Event Indication */
#define SDPCM_DATA_CHANNEL		2	/* Data Xmit/Recv */
#define SDPCM_GLOM_CHANNEL		3	/* Coalesced packets */
#define SDPCM_TEST_CHANNEL		15	/* Test/debug packets */
#define SDPCM_GLOMDESC(p)		(((u8 *)p)[1] & 0x80)
#define SDPCM_NEXTLEN_MASK		0x00ff0000
#define SDPCM_NEXTLEN_SHIFT		16
#define SDPCM_DOFFSET_MASK		0xff000000
#define SDPCM_DOFFSET_SHIFT		24
#define SDPCM_FCMASK_MASK		0x000000ff
#define SDPCM_WINDOW_MASK		0x0000ff00
#define SDPCM_WINDOW_SHIFT		8

struct PacketHeaderHW {
    UWORD ph_Length;
    UWORD ph_ChkSum;
} __attribute__((packed));

struct GlomHeader {
    UWORD gh_Length;
    UBYTE gh_ReservedB;
    UBYTE gh_LastItem;
    UWORD gh_ReservedW;
    UWORD gh_TailPad;
} __attribute__((packed));

struct PacketHeaderSW {
    UBYTE c_Seq;
    UBYTE c_ChannelFlag;
    UBYTE c_NextLength;
    UBYTE c_DataOffset;
    UBYTE c_FlowControl;
    UBYTE c_MaxSeq;
    UBYTE c_Reserved[2];
} __attribute__((packed));

struct Packet {
    // Hardware header
    UWORD p_Length;
    UWORD c_ChkSum;
    // Software header
    UBYTE c_Seq;
    UBYTE c_ChannelFlag;
    UBYTE c_NextLength;
    UBYTE c_DataOffset;
    UBYTE c_FlowControl;
    UBYTE c_MaxSeq;
    UBYTE c_Reserved[2];
} __attribute__((packed));

#define BCDC_DCMD_ERROR		0x01		/* 1=cmd failed */
#define BCDC_DCMD_SET		0x02		/* 0=get, 1=set cmd */

struct PacketCmd {
    ULONG c_Command;
    ULONG c_Length;
    UWORD c_Flags;
    UWORD c_ID;
    ULONG c_Status;
} __attribute__((packed));

struct EtherHeader {
    UBYTE eh_Dest[6];
    UBYTE eh_Src[6];
    UWORD eh_Type;
} __attribute__((packed));

#define ETHERHDR_TYPE_LINK_CTL  0x886c

struct BCMEtherHeader {
    UWORD beh_Subtype;
    UWORD beh_Length;
    UBYTE beh_Version;
    UBYTE beh_OUI[3];
    UWORD beh_UsrSubtype;
} __attribute__((packed));

#define BCMETHHDR_OUI   "\0x00\0x10\0x18"
#define BCMETHHDR_SUBTYPE_EVENT 1

struct EtherAddr {
    UBYTE ea_Addr[6];
} __attribute__((packed));

struct PacketEvent {
    struct EtherHeader      e_EthHeader;
    struct BCMEtherHeader   e_Header;
    UWORD                   e_Version;
    UWORD                   e_Flags;
    ULONG                   e_EventType;
    ULONG                   e_Status;
    ULONG                   e_Reason;
    ULONG                   e_AuthType;
    ULONG                   e_DataLen;
    struct EtherAddr        e_Address;
    char                    e_IFName[16];
    UBYTE                   e_IFIdx;
    UBYTE                   e_BSSCFgIdx;
} __attribute__((packed));

struct BSSInfo {
    ULONG   bssi_Version;
    ULONG   bssi_Length;
    UBYTE   bssi_ID[6];
    UWORD   bssi_BeaconPeriod;
    UWORD   bssi_Capability;
    UBYTE   bssi_SSIDLength;
    UBYTE   bssi_SSID[32];
    UBYTE   PAD;
    ULONG   bssi_NRates;
    UBYTE   bssi_Rates[16];
    UWORD   bssi_ChanSpec;      // 72
    UWORD   bssi_ATimWindow;
    UBYTE   bssi_DTimPeriod;
    UBYTE   PAD;
    UWORD   bssi_RSSI;
    UBYTE   bssi_PHYNoise;
    UBYTE   bssi_NCap;
    UWORD   PAD;
    ULONG   bssi_NBSSCap;
    UBYTE   bssi_CtlCh;
    UBYTE   PAD[3];
    ULONG   bssi_Reserved32[1];
    UBYTE   bssi_Flags;
    UBYTE   bssi_Reserved[3];
    UBYTE   bssi_BasicMCS[16];
    UWORD   bssi_IEOffset;
    UWORD   PAD;
    ULONG   bssi_IELength;
    UWORD   bssi_SNR;
} __attribute__((packed));

struct EScanResult {
    ULONG   esr_Length;
    ULONG   esr_Version;
    UWORD   esr_SyncID;
    UWORD   esr_BSSCount;
    struct BSSInfo esr_BSSInfo[];
} __attribute__((packed));

struct PacketMessage {
    struct Message  pm_Message;
    APTR            pm_RecvBuffer;
    ULONG           pm_RecvSize;
    APTR            pm_PacketData;
    struct Packet   pm_PacketHeader[];
};


#define D(x) x

#define VENDOR_SPECIFIC_IE  221
#define WLAN_EID_RSN 48



/*
static const UBYTE WPA_OUI_TYPE[] = {0x00, 0x50, 0xf2, 1};
static const UBYTE WPA_CIPHER_SUITE_NONE[] = {0x00, 0x50, 0xf2, 0};
static const UBYTE WPA_CIPHER_SUITE_WEP40[] = {0x00, 0x50, 0xf2, 1};
static const UBYTE WPA_CIPHER_SUITE_TKIP[] = {0x00, 0x50, 0xf2, 2};
static const UBYTE WPA_CIPHER_SUITE_CCMP[] = {0x00, 0x50, 0xf2, 4};
static const UBYTE WPA_CIPHER_SUITE_WEP104[] = {0x00, 0x50, 0xf2, 5};

static const UBYTE RSN_CIPHER_SUITE_NONE[] = {0x00, 0x0f, 0xac, 0};
static const UBYTE RSN_CIPHER_SUITE_WEP40[] = {0x00, 0x0f, 0xac, 1};
static const UBYTE RSN_CIPHER_SUITE_TKIP[] = {0x00, 0x0f, 0xac, 2};
static const UBYTE RSN_CIPHER_SUITE_CCMP[] = {0x00, 0x0f, 0xac, 4};
static const UBYTE RSN_CIPHER_SUITE_WEP104[] = {0x00, 0x0f, 0xac, 5};
*/



/* Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
struct TLV * brcmf_parse_tlvs(void *buf, ULONG buflen, UBYTE key)
{
    struct TLV *elt = buf;
    ULONG totlen = buflen;

    /* find tagged parameter */
    while (totlen >= TLV_HDR_LEN) {
        ULONG len = elt->len;

        /* validate remaining totlen */
        if ((elt->id == key) && (totlen >= (len + TLV_HDR_LEN)))
            return elt;

        elt = (struct TLV*)((UBYTE *)elt + (len + TLV_HDR_LEN));
        totlen -= (len + TLV_HDR_LEN);
    }

    return NULL;
}

int my_memcmp(const UBYTE *s1, const UBYTE *s2, ULONG len)
{
    for (ULONG i=0; i < len; i++)
    {
        if (s1[i] > s2[i]) return 1;
        else if (s1[i] < s2[i]) return -1;
    }
    return 0;
}

static int brcmf_tlv_has_ie(UBYTE *ie, UBYTE **tlvs, ULONG *tlvs_len,
		 const UBYTE *oui, ULONG oui_len, UBYTE type)
{
    /* If the contents match the OUI and the type */
    if (ie[TLV_LEN_OFF] >= oui_len + 1 &&
        !my_memcmp(&ie[TLV_BODY_OFF], oui, oui_len) &&
        type == ie[TLV_BODY_OFF + oui_len]) {
        return TRUE;
    }

    if (tlvs == NULL)
        return FALSE;

    /* point to the next ie */
    ie += ie[TLV_LEN_OFF] + TLV_HDR_LEN;
    /* calculate the length of the rest of the buffer */
    *tlvs_len -= (int)(ie - *tlvs);
    /* update the pointer to the start of the buffer */
    *tlvs = ie;

    return FALSE;
}

struct VsTLV * FindWPAIE(UBYTE *data, ULONG len)
{
    const struct TLV *ie;

    while ((ie = brcmf_parse_tlvs(data, len, VENDOR_SPECIFIC_IE)))
    {
        if (brcmf_tlv_has_ie((UBYTE *)ie, &data, &len,
                        WPA_OUI, TLV_OUI_LEN, WPA_OUI_TYPE))
            return (struct VsTLV *)ie;
    }
    return NULL;
}

#define WPA_VERSION_1   1
#define WPA_VERSION_2   2
#define WPA_VERSION_3   4

int SetWPAVersion(struct SDIO *sdio, struct WiFiNetwork *network, ULONG wpa_versions)
{
    ULONG val = 0;
    (void)network;
    (void)wpa_versions;

    if (wpa_versions & WPA_VERSION_1)
        val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
    else if (wpa_versions & WPA_VERSION_2)
        val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
    else if (wpa_versions & WPA_VERSION_3)
        val = WPA3_AUTH_SAE_PSK;
    else
        val = WPA_AUTH_DISABLED;

    return PacketSetVarInt(sdio, "wpa_auth", val);
}

#define SCANNER_STACKSIZE       (16384 / sizeof(ULONG))
#define SCANNER_PRIORITY         0


#define PACKET_RECV_STACKSIZE   (65536 / sizeof(ULONG))
#define PACKET_RECV_PRIORITY    5

#define PACKET_WAIT_DELAY_MIN   1000
#define PACKET_WAIT_DELAY_MAX   100000

#define PACKET_INITIAL_FETCH_SIZE   16

void PacketDump(struct SDIO *sdio, APTR data, char *src);

struct TagItem * FindNetwork(struct WiFiUnit *unit, struct BSSInfo *info)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct SDIO *sdio = WiFiBase->w_SDIO;
    struct Library *UtilityBase = WiFiBase->w_UtilityBase;
    struct IOSana2Req *io = unit->wu_ScanRequest;
    struct TagItem *found = NULL;
    struct TagItem **list = (struct TagItem **)io->ios2_StatData;
    
    if (list != NULL)
    {
        for (ULONG i=0; i < io->ios2_DataLength; i++)
        {
            struct TagItem *tags = list[i];
            UBYTE *bssid;
            UBYTE *ssid;
            /*
                Two networks are considered the same if:
                - SSID is same
                - BSSID is same
                - Channel and band are the same
            */

            /* Check SSID */
            ssid = (UBYTE*)GetTagData(S2INFO_SSID, 0, tags);
            
            /* Must not be null! */
            if (ssid == NULL)
                continue;

            /* SSID length must match */
            if (_strlen(ssid) != info->bssi_SSIDLength)
                continue;

            /* SSID must match */
            if (_strncmp(ssid, info->bssi_SSID, info->bssi_SSIDLength) != 0)
                continue;

            /* Check BSSID */
            bssid = (UBYTE*)GetTagData(S2INFO_BSSID, 0, tags);

            /* Must not be null */
            if (bssid == NULL)
                continue;
            
            /* Must be the same */
            if (bssid[0] != info->bssi_ID[0] ||
                bssid[1] != info->bssi_ID[1] ||
                bssid[2] != info->bssi_ID[2] ||
                bssid[3] != info->bssi_ID[3] ||
                bssid[4] != info->bssi_ID[4] ||
                bssid[5] != info->bssi_ID[5])
                continue;
            
            /* Finally, check channel */
            struct ChannelInfo ci;
            ci.ci_CHSpec = LE16(info->bssi_ChanSpec);
            DecodeChanSpec(&ci, sdio->s_Chip->c_D11Type);

            ULONG band = GetTagData(S2INFO_Band, -1, tags);
            if ((ci.ci_Band == BRCMU_CHAN_BAND_2G && band != S2BAND_B) ||
                (ci.ci_Band != BRCMU_CHAN_BAND_2G && band != S2BAND_A))
                continue;

            ULONG channel = GetTagData(S2INFO_Channel, 0, tags);
            if (channel != ci.ci_CHNum)
                continue;

            found = tags;
            break;
        }
    }

    return found;
}

void UpdateNetwork(struct WiFiUnit *unit, struct BSSInfo *info)
{
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct SDIO *sdio = WiFiBase->w_SDIO;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct IOSana2Req *io = unit->wu_ScanRequest;
    struct TagItem *net;

    /* Ignore event if no scan request is active */
    if (io == NULL)
    {
        return;
    }

    /* Ignore network duplicates, later maybe update them */
    if ((net = FindNetwork(unit, info)))
    {
        return;
    }

    /* Ignore networks with empty ssid */
    if (info->bssi_SSIDLength == 0)
        return;

    APTR memPool = io->ios2_Data;
    struct TagItem **networks = NULL;
    struct TagItem *tags;

    /* Increase tag list counter */
    io->ios2_DataLength++;

    /* Get memory for tag list's list */
    networks = AllocVecPooled(memPool, 4 * io->ios2_DataLength);
    if (networks == NULL)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        unit->wu_ScanRequest = NULL;
        ReplyMsg((struct Message *)io);
        return;
    }

    /* 
        If StatData is not null, copy it now and free. After copy networks[1..] will contain
        previous data, current network can be stored at networks[0]
    */
    if (io->ios2_StatData != NULL)
    {
        struct TagItem **src = io->ios2_StatData;
        for (ULONG i=0; i < io->ios2_DataLength - 1; i++)
        {
            networks[i+1] = src[i];
        }
        /* Get rid of old taglist */
        FreeVecPooled(memPool, src);
    }

    io->ios2_StatData = networks;
    
    /* Get memory for TagList, maximal number is number of S2INFO_TAGS plus one */
    tags = AllocPooled(memPool, sizeof(struct TagItem) * 16);
    
    if (tags == NULL)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        unit->wu_ScanRequest = NULL;
        ReplyMsg((struct Message *)io);
        return;
    }

    /* Ignore all tags for now */
    for (int i=0; i < 15; i++) tags[i].ti_Tag = TAG_IGNORE;
    
    /* Finish with DONE tag*/
    tags[15].ti_Tag = TAG_DONE;
    tags[15].ti_Data = 0;

    /* Put the taglist in place */
    networks[0] = tags;

    /* All is prepared to fill the necessary info */
    tags->ti_Tag = S2INFO_SSID;
    tags->ti_Data = (ULONG)AllocPooledClear(memPool, info->bssi_SSIDLength + 1);
    if (tags->ti_Data == 0)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        unit->wu_ScanRequest = NULL;
        ReplyMsg((struct Message *)io);
        return;
    }
    CopyMem(info->bssi_SSID, (APTR)tags->ti_Data, info->bssi_SSIDLength);
    tags++;

    tags->ti_Tag = S2INFO_BSSID;
    tags->ti_Data = (ULONG)AllocPooled(memPool, 6);
    if (tags->ti_Data == 0)
    {
        io->ios2_WireError = S2WERR_BUFF_ERROR;
        io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
        unit->wu_ScanRequest = NULL;
        ReplyMsg((struct Message *)io);
        return;
    }
    CopyMem(info->bssi_ID, (APTR)tags->ti_Data, 6);
    tags++;

    tags->ti_Tag = S2INFO_BeaconInterval;
    tags->ti_Data = LE16(info->bssi_BeaconPeriod);
    tags++;

    tags->ti_Tag = S2INFO_Signal;
    tags->ti_Data = (WORD)LE16(info->bssi_RSSI);
    tags++;

    if (info->bssi_PHYNoise != 0)
    {
        tags->ti_Tag = S2INFO_Noise;
        tags->ti_Data = (BYTE)info->bssi_PHYNoise;
        tags++;
    }

    if (info->bssi_IELength)
    {
        UWORD length = LE32(info->bssi_IELength);
        UWORD *data = AllocPooled(memPool, length + 2);
        if (data == NULL)
        {
            io->ios2_WireError = S2WERR_BUFF_ERROR;
            io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;
            unit->wu_ScanRequest = NULL;
            ReplyMsg((struct Message *)io);
            return;
        }

        *data = length;
        CopyMem(&((UBYTE*)info)[LE16(info->bssi_IEOffset)], data + 1, length);
        
        tags->ti_Tag = S2INFO_InfoElements;
        tags->ti_Data = (ULONG)data;
        tags++;
    }

    tags->ti_Tag = S2INFO_Capabilities;
    tags->ti_Data = LE16(info->bssi_Capability);
    tags++;

    struct ChannelInfo ci;
    ci.ci_CHSpec = LE16(info->bssi_ChanSpec);
    DecodeChanSpec(&ci, sdio->s_Chip->c_D11Type);

    tags->ti_Tag = S2INFO_Channel;
    tags->ti_Data = ci.ci_CHNum;
    tags++;

    tags->ti_Tag = S2INFO_Band;
    tags->ti_Data = ci.ci_Band == BRCMU_CHAN_BAND_2G ? S2BAND_B : S2BAND_A;
    tags++;
}

void ProcessEvent(struct SDIO *sdio, struct PacketEvent *pe)
{
    struct WiFiBase *base = sdio->s_WiFiBase;
    struct WiFiUnit *unit = base->w_Unit;
    struct ExecBase *SysBase = base->w_SysBase;

    // pe is in network (BigEndian) order!
    // BUT! pe data is native (LittleEndian) order!
    switch (pe->e_EventType)
    {
        case BRCMF_E_ESCAN_RESULT:
        {
            struct EScanResult *escan = (APTR)((ULONG)pe + sizeof(struct PacketEvent));
            //D(bug("[WiFi] EScan result. Length %ld, BSS count %ld\n", LE32(escan->esr_Length), LE16(escan->esr_BSSCount)));
            
            if (escan->esr_BSSCount == LE16(0))
            {
                // Scan complete
                if (unit->wu_ScanRequest)
                {
                    ReplyMsg((struct Message *)unit->wu_ScanRequest);
                    unit->wu_ScanRequest = NULL;
                }
                //D(bug("[WiFi] EScan complete\n"));
            }

            for (int i=0; i < LE16(escan->esr_BSSCount); i++)
            {
                UpdateNetwork(unit, &escan->esr_BSSInfo[i]);
            }
            break;
        }

        case BRCMF_E_ASSOC:
            D(bug("[WiFi] E_ASSOC\n"));
            if (unit->wu_AssocIE) FreeVecPooled(base->w_MemPool, unit->wu_AssocIE);
            unit->wu_AssocIE = NULL;
            unit->wu_AssocIELength = pe->e_DataLen;
            if (unit->wu_AssocIELength)
            {
                unit->wu_AssocIE = AllocVecPooled(base->w_MemPool, pe->e_DataLen);
                if (unit->wu_AssocIE != NULL)
                {
                    CopyMem(((UBYTE*)pe) + sizeof(struct PacketEvent), unit->wu_AssocIE, pe->e_DataLen);
                }
            }
            CopyMem(&pe->e_Address, unit->wu_JoinParams.ej_Assoc.ap_BSSID, 6);
            break;
        
        case BRCMF_E_AUTH:
            D(bug("[WiFi] E_AUTH "));
            if (pe->e_Status == 0) {
                D(bug("OK\n"));
            }
            else
            {
                D(bug("Failed with reason %08lx\n", pe->e_Reason));
            }
            break;

        case BRCMF_E_DISASSOC:
            D(bug("[WiFi] E_DISASSOC\n"));
            {
                UBYTE *p = (APTR)pe;
                for (ULONG i=0; i < sizeof(struct PacketEvent) + pe->e_DataLen; i++)
                {
                    if (i % 16 == 0)
                    bug("[WiFI]  ");
                    bug(" %02lx", p[i]);
                    if (i % 16 == 15)
                        bug("\n");
                }
                if ((sizeof(struct PacketEvent) + pe->e_DataLen) % 16)
                    bug("\n");
            }
            break;
        
        case BRCMF_E_REASSOC:
            D(bug("[WiFi] E_DISASSOC\n"));
            {
                UBYTE *p = (APTR)pe;
                for (ULONG i=0; i < sizeof(struct PacketEvent) + pe->e_DataLen; i++)
                {
                    if (i % 16 == 0)
                    bug("[WiFI]  ");
                    bug(" %02lx", p[i]);
                    if (i % 16 == 15)
                        bug("\n");
                }
                if ((sizeof(struct PacketEvent) + pe->e_DataLen) % 16)
                    bug("\n");
            }
            break;

        case BRCMF_E_LINK:
            if (pe->e_Reason)
            {
                D(bug("[WiFi] E_LINK down\n"));
                unit->wu_Flags &= ~IFF_CONNECTED;
                ReportEvents(unit, S2EVENT_DISCONNECT);
            }
            else
            {
                D(bug("[WiFi] E_LINK up\n"));
                unit->wu_Flags |= IFF_CONNECTED;
                ReportEvents(unit, S2EVENT_CONNECT);
            }
            break;

        default:
            D(bug("[WiFi] Unhandled event type %ld, status %08lx, reason %08lx\n", pe->e_EventType, pe->e_Status, pe->e_Reason));
            UBYTE *p = (APTR)pe;
            for (ULONG i=0; i < sizeof(struct PacketEvent) + pe->e_DataLen; i++)
            {
                if (i % 16 == 0)
                bug("[WiFI]  ");
                bug(" %02lx", p[i]);
                if (i % 16 == 15)
                    bug("\n");
            }
            if ((sizeof(struct PacketEvent) + pe->e_DataLen) % 16)
                bug("\n");
            break;
    }
}

ULONG ProcessPacket(struct SDIO *sdio, struct Packet *pkt)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    UBYTE *buffer = (UBYTE*)pkt;

    UWORD pktLen = LE16(pkt->p_Length);
    UWORD pktChk = LE16(pkt->c_ChkSum);

    /* Both null size - empty packet */
    if (pktLen == 0 && pktChk == 0) return 0;

    /* Length and checksum not matching - error */
    if (pktLen != (~pktChk & 0xffff)) return 0xffffffff;

    /* Update max sequence number at transfer */
    sdio->s_MaxTXSeq = pkt->c_MaxSeq;

    switch(pkt->c_ChannelFlag)
    {
        case SDPCM_CONTROL_CHANNEL:
        { 
            // Control channel contains commands only. Get it.
            struct PacketCmd *cmd = (APTR)&buffer[pkt->c_DataOffset];

            // Go through control wait list. If message is found with given ID, reply it
            // No need to lock the list, it is accessed only in this task
            struct PacketMessage *m;
            ForeachNode(sdio->s_CtrlWaitList, m)
            {
                struct PacketCmd *c = (APTR)m->pm_PacketData;

                // If the ID of waiting packet and received packet match, copy the received
                // Data back into the message and reply it
                if (c->c_ID == cmd->c_ID)
                {
                    // Message match. Remove it from wait list.
                    Remove(&m->pm_Message.mn_Node);

                    // Warn in case of length mismatch. Should not be the case though!
                    if (pktLen != LE16(pkt->p_Length))
                    {
                        //D(bug("[WiFi.RECV] Length mismatch %ld!=%ld\n", pktLen, LE16(pkt->p_Length)));
                    }

                    // Copy PacketCmd back to buffer
                    CopyMem(cmd, c, sizeof(struct PacketCmd));

                    // If get-type of packet and no error flag was set, copy data back
                    if (!(c->c_Flags & LE16(BCDC_DCMD_SET)))
                    {
                        if (!(c->c_Flags & LE16(BCDC_DCMD_ERROR)))
                        {
                            if (m->pm_RecvBuffer != NULL && m->pm_RecvSize != 0)
                            {
                                // RecvBuffer and RecvSize are given, there was no error and
                                // packet type is "Get"
                                // Copy data back now
                                CopyMem((UBYTE*)cmd + sizeof(struct PacketCmd), m->pm_RecvBuffer, m->pm_RecvSize);
                            }
                        }
                    }

                    // Reply back to sender
                    ReplyMsg(&m->pm_Message);
                    break;
                }
            }
            break;
        }

        case SDPCM_EVENT_CHANNEL:
        {
            struct PacketEvent *pe = (APTR)&buffer[pkt->c_DataOffset + 4];

            if ((ULONG)(pkt->p_Length - pkt->c_DataOffset) >= sizeof(struct PacketEvent))
            {
                if (pe->e_EthHeader.eh_Type == ETHERHDR_TYPE_LINK_CTL && 
                    pe->e_Header.beh_OUI[0] == 0x00 && 
                    pe->e_Header.beh_OUI[1] == 0x10 && 
                    pe->e_Header.beh_OUI[2] == 0x18)
                {
                    if (pe->e_Header.beh_UsrSubtype == BCMETHHDR_SUBTYPE_EVENT)
                    {
                        ProcessEvent(sdio, pe);
                    }
                }
            }
            break;
        }
        
        case SDPCM_DATA_CHANNEL:
        {
            UBYTE *frame = (APTR)&buffer[pkt->c_DataOffset + 4];
            ULONG frameLength = pktLen - pkt->c_DataOffset - 4;

            ProcessDataPacket(sdio, frame, frameLength);

            break;
        }
    }

    return pktLen;
}

int SendGlomDataPacket(struct SDIO *sdio, struct IOSana2Req **ioList, UBYTE count);

void PacketReceiver(struct SDIO *sdio, struct Task *caller)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    ULONG waitDelay = PACKET_WAIT_DELAY_MAX;
    struct MsgPort *ctrl = CreateMsgPort();
    struct MsgPort *sender = CreateMsgPort();
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    struct TimerBase *TimerBase;

    struct MinList ctrlWaitList;
    ULONG waitDelayTimeout = PACKET_WAIT_DELAY_MAX / waitDelay;

    NewMinList(&ctrlWaitList);

    /* Sender port is signal-free */
    FreeSignal(sender->mp_SigBit);
    sender->mp_Flags = PA_IGNORE;

    D(bug("[WiFi.RECV] Packet receiver task\n"));
    D(bug("[WiFi.RECV] SDIO=%08lx, Caller task=%08lx\n", (ULONG)sdio, (ULONG)caller));

    if (ctrl == NULL)
    {
        D(bug("[WiFi.RECV] Failed to create command port\n"));
        return;
    }

    // Create MessagePort and timer.device IORequest
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *tr = (struct timerequest *)CreateIORequest(port, sizeof(struct timerequest));

    if (port == NULL || tr == NULL)
    {
        D(bug("[WiFi.RECV] Failed to create IO Request\n"));
        if (port != NULL) DeleteMsgPort(port);
        Signal(caller, SIGBREAKF_CTRL_C);
        return;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ, &tr->tr_node, 0))
    {
        D(bug("[WiFi.RECV] Failed to open timer.device\n"));
        DeleteIORequest(&tr->tr_node);
        DeleteMsgPort(port);
        Signal(caller, SIGBREAKF_CTRL_C);
        return;
    }

    TimerBase = (struct TimerBase *)tr->tr_node.io_Device;

    // Set up receiver task pointer in SDIO
    sdio->s_ReceiverTask = FindTask(NULL);

    // Create message port used by receiver
    sdio->s_ReceiverPort = ctrl;
    sdio->s_SenderPort = sender;
    sdio->s_CtrlWaitList = &ctrlWaitList;

    // Signal caller that we are done with setup
    Signal(caller, SIGBREAKF_CTRL_C);

    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_sec = waitDelay / 1000000;
    tr->tr_time.tv_micro = waitDelay % 1000000;
    SendIO(&tr->tr_node);

    // Clear PACKET_INITIAL_FETCH_SIZE bytes of RX buffer
    UBYTE *buffer = sdio->s_RXBuffer;
    struct Packet *pkt = sdio->s_RXBuffer;

    for (int i=0; i < PACKET_INITIAL_FETCH_SIZE; i++) buffer[i] = 0;

    // Loop forever
    UBYTE gotTransfer = 0;
    UBYTE sendTransfer = 0;
    UBYTE controlTransfer = 0;

    while(1)
    {
        ULONG sigSet;

        /* If previous time there was a send or recive transfer, do not wait at all */
        if (sendTransfer || gotTransfer)
        {
            sigSet = SetSignal(0, SIGBREAKF_CTRL_C | 
                         (1 << port->mp_SigBit) |
                         (1 << ctrl->mp_SigBit));
        }
        else
        {
            sigSet = Wait(SIGBREAKF_CTRL_C | 
                         (1 << port->mp_SigBit) |
                         (1 << ctrl->mp_SigBit));
        }
        
        sendTransfer = 0;
        gotTransfer = 0;
        controlTransfer = 0;
        UWORD lastLength = 0;

        // Signal from control message port?
        if (sigSet & (1 << ctrl->mp_SigBit))
        {
            struct PacketMessage *msg;

            // Repeat until we run out of the messages
            while((msg = (struct PacketMessage *)GetMsg(ctrl)))
            {
                // Put message in the control wait list
                AddTail((struct List*)&ctrlWaitList, &msg->pm_Message.mn_Node);

                // Send out the control packet
                sdio->SendPKT((APTR)&msg->pm_PacketHeader[0], LE16(msg->pm_PacketHeader[0].p_Length), sdio);
                lastLength = LE16(msg->pm_PacketHeader[0].p_Length);
                controlTransfer++;
            }
        }

        // Always check if there are data packets for sending
        if (TRUE && controlTransfer == 0)
        {
            //struct IOSana2Req *ioList[32];
            struct IOSana2Req *msg;
            //ULONG ioCount = 0;
            UBYTE maxCount;

            maxCount = sdio->s_MaxTXSeq - sdio->s_TXSeq;

            /* Make sure we have place in TX */
            if (maxCount)
            {
                // Drain outgoing packet requests
                while ((msg = (struct IOSana2Req *)GetMsg(sender)))
                {
                    sendTransfer = TRUE;

                    SendDataPacket(sdio, msg);

                    // Put the packet into an array. It will be used later to construct Glom frame
#if 0
                    ioList[ioCount++] = msg;
#endif

                    if (--maxCount == 0)
                    {
                        //D(bug("[WiFi] No more place in TX\n"));
                        break;
                    }
#if 0
                    // Glom full? Push out large frame
                    // But not yet, for now just send them all out, one after another
                    if (ioCount == 32)
                    {
                        //D(bug("[WiFi] Glom frame would do, there are %ld entries in queue\n", ioCount));
                        /*
                        for (ULONG i=0; i < ioCount; i++)
                        {
                            SendDataPacket(sdio, ioList[i]);
                            ReplyMsg((struct Message *)ioList[i]);
                        }
                        */
                        SendGlomDataPacket(sdio, ioList, ioCount);
                        ioCount = 0;
                    }
#endif
                }

                // Any write requests left? Push them out now
                // One item only? Send it as one packet.
                #if 0
                 if (ioCount == 1)
                {
                    SendDataPacket(sdio, ioList[0]);
                    ReplyMsg((struct Message *)ioList[0]);
                }
                else 
                #endif
#if 0
                if (ioCount)
                {
                    //D(bug("[WiFi] Glom frame would do, there are %ld entries in queue\n", ioCount));
                    // More items? Construct glom frame
                    SendGlomDataPacket(sdio, ioList, ioCount);
                    /*
                    for (ULONG i=0; i < ioCount; i++)
                    {
                        SendDataPacket(sdio, ioList[i]);
                        ReplyMsg((struct Message *)ioList[i]);
                    }
                    */
                }
#endif
            }
        }

        if (sendTransfer && controlTransfer)
        {
            D(bug("[WiFi] ---- Send transfer and control transfer (count %ld, length %ld) in same frame\n", controlTransfer, lastLength));
        }

        /* If no scan request is in progress start another one (if needed) */
        if (WiFiBase->w_Unit && WiFiBase->w_Unit->wu_ScanRequest == NULL)
        {
            struct IOSana2Req *io = (struct IOSana2Req *)GetMsg(WiFiBase->w_Unit->wu_ScanQueue);
            if (io)
            {
                StartNetworkScan(io);
            }
        }

        // Signal from timer.device or from control message port?
        // Both are great occasions to test if some data is pending
        if (sigSet & ((1 << port->mp_SigBit) | (1 << ctrl->mp_SigBit)))
        {
            if (WiFiBase->w_Unit->wu_GlomCount > 0)
            {
                struct timeval now;
                GetSysTime(&now);
                SubTime(&now, &WiFiBase->w_Unit->wu_GlomCreationTime);

                /* If glom frame is older than 0.0002 second, send it out now */
                if (now.tv_sec > 0 || now.tv_micro > 200)
                {
                    struct PacketHeaderHW *pktBase = sdio->s_TXBuffer;
                    //D(bug("[WiFi.0] Sent partially filled glom packet with count %ld, frame age: %ld:%06ld\n", WiFiBase->w_Unit->wu_GlomCount, now.tv_sec, now.tv_micro));

                    *WiFiBase->w_Unit->wu_GlomLastItemMarker = 1;
                    sdio->SendPKT((UBYTE *)pktBase, LE16(pktBase->ph_Length), sdio);
#if 0
                    for (unsigned int i=0; i < WiFiBase->w_Unit->wu_GlomCount; i++) { 
                        ReplyMsg((struct Message *)WiFiBase->w_Unit->wu_GlomQueue[i]);
                    }

                    WiFiBase->w_Unit->wu_Stats.PacketsSent+=WiFiBase->w_Unit->wu_GlomCount;
#endif
                    WiFiBase->w_Unit->wu_GlomCount = 0;
                }
            }

            if (sigSet & (1 << ctrl->mp_SigBit))
            {
                AbortIO(&tr->tr_node);
                WaitIO(&tr->tr_node);
            }

            sdio->RecvPKT(buffer, PACKET_INITIAL_FETCH_SIZE, sdio);

            /* Update gotTransfer flag if it wasn't set already */
            gotTransfer = LE16(pkt->p_Length) != 0;

            if (sigSet & (1 << port->mp_SigBit))
            {
                // Check if IO really completed. If yes, remove it from the queue
                if (CheckIO(&tr->tr_node))
                {
                    WaitIO(&tr->tr_node);
                }
            
                if (gotTransfer || sendTransfer)
                {
                    waitDelay = PACKET_WAIT_DELAY_MIN;
                }
                else
                {
                    if (waitDelayTimeout)
                    {
                        waitDelayTimeout--;
                    } 
                    else if (waitDelay < PACKET_WAIT_DELAY_MAX)
                    {
                        waitDelay <<= 2;
                        if (waitDelay > PACKET_WAIT_DELAY_MAX) waitDelay = PACKET_WAIT_DELAY_MAX;
                        waitDelayTimeout = PACKET_WAIT_DELAY_MAX / waitDelay;
                    }
                }

                // Fire new IORequest
                tr->tr_node.io_Command = TR_ADDREQUEST;
                tr->tr_time.tv_sec = waitDelay / 1000000;
                tr->tr_time.tv_micro = waitDelay % 1000000;
                SendIO(&tr->tr_node);
            }

            if (gotTransfer)
            {
                UWORD pktLen = LE16(pkt->p_Length);
                UWORD pktChk = LE16(pkt->c_ChkSum);
                
                if ((pktChk | pktLen) == 0xffff)
                {
                    // Until now we have fetched PACKET_INITIAL_FETCH_SIZE bytes only. If packet length is larger, fetch 
                    // the rest now
                    if (pktLen > PACKET_INITIAL_FETCH_SIZE)
                    {
                        sdio->RecvPKT(&buffer[PACKET_INITIAL_FETCH_SIZE], pktLen - PACKET_INITIAL_FETCH_SIZE, sdio);
                    }

                    if ((pkt->c_ChannelFlag & 15) == SDPCM_GLOM_CHANNEL)
                    {
                        if (pkt->c_ChannelFlag & 0x80)
                        {
                            #if 0
                            UBYTE *bdata = (UBYTE*)buffer;
                            ULONG dataLength = pktLen;

                            bug("[WiFi.GLOM_0x80] Glom frame with flagged channel\n");

                            for (ULONG i=0; i < dataLength; i++)
                            {
                                if (i % 16 == 0)
                                    bug("[WiFi.GLOM_0x80] %04x ", i);
                                bug(" %02lx", bdata[i]);
                                if (i % 16 == 15)
                                    bug("\n");
                            }
                            if (dataLength % 16 != 0) bug("\n");
                            #endif
                        }
                        else
                        {
                            ULONG pos = pkt->c_DataOffset;

                            while(pos < pktLen)
                            {
                                struct Packet *epkt = (APTR)&buffer[pos];

                                ULONG processed = ProcessPacket(sdio, epkt);

                                if (processed == 0)
                                {
                                    D(bug("[WiFi] Last glom element\n"));
                                    break;
                                }
                                else if (processed == 0xffffffff)
                                {
                                    D(bug("[WiFi] Frame error\n"));
                                    break;
                                }
                                else
                                {
                                    pos += processed;
                                    pos = (pos + 3) & ~3;
                                }
                            }
                        }
                    }
                    else
                    {
                        ProcessPacket(sdio, pkt);
                    }

                    // Mark that we have the transfer, we will wait for next one a bit shorter
                    gotTransfer = 1;
                }
                else
                {
                    D(bug("[WiFi.RECV] Garbage received. Data:\n"));
                    for (int i=0; i < 256; i++)
                    {
                        if (i % 16 == 0)
                            bug("[WiFi]  ");
                        bug(" %02lx", buffer[i]);
                        if (i % 16 == 15)
                            bug("\n");
                    }
                }
            }
        }

        // Shutdown signal?
        if (sigSet & SIGBREAKF_CTRL_C)
        {
            D(bug("[WiFi.RECV] Quiting receiver loop\n"));
            // Abort timer IO
            AbortIO(&tr->tr_node);
            WaitIO(&tr->tr_node);
            break;
        }
    }

    D(bug("[WiFi.RECV] Packet receiver is closing now\n"));
    CloseDevice(&tr->tr_node);
    DeleteIORequest(&tr->tr_node);
    DeleteMsgPort(port);
    DeleteMsgPort(ctrl);
    sdio->s_ReceiverTask = NULL;
}

void CopyPacket(struct IOSana2Req *io, UBYTE *packet, ULONG packetLength)
{
    struct WiFiUnit *unit = (struct WiFiUnit *)io->ios2_Req.io_Unit;
    struct WiFiBase *WiFiBase = unit->wu_Base;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct Opener *opener = io->ios2_BufferManagement;
    struct Library *UtilityBase = WiFiBase->w_UtilityBase;
    UBYTE packetFiltered = FALSE;
    APTR src;
    APTR dst;

    UBYTE *copyData;
    ULONG copyLength;

    UWORD type = *(UWORD*)&packet[12];

    /* Clear broadcast and multicast flags */
    io->ios2_Req.io_Flags &= ~(SANA2IOF_BCAST | SANA2IOF_MCAST);

    /*
        Copy source and dest addresses
        Go ugly route to avoid breaking of strict-aliasing rules, avoid loop copying because
        gcc will not optimize that assuming that UBYTE can be unaligned, avoid calling CopyMem
        because copying 6 bytes two times is not worth the overhead
    */
    src = &packet[0];
    dst = &io->ios2_DstAddr[0];
    ((ULONG*)dst)[0] = ((ULONG*)src)[0];
    ((UWORD*)dst)[2] = ((UWORD*)src)[2];
    
    src = &packet[6];
    dst = &io->ios2_SrcAddr[0];
    ((ULONG*)dst)[0] = ((ULONG*)src)[0];
    ((UWORD*)dst)[2] = ((UWORD*)src)[2];

    io->ios2_PacketType = type;

    /* If dest address is FF:FF:FF:FF:FF:FF then it is a broadcast */
    if (*(ULONG*)packet == 0xffffffff && *(UWORD*)(packet+4) == 0xffff)
    {
        io->ios2_Req.io_Flags |= SANA2IOF_BCAST;
    }
    /* If dest address has lowest bit of first addr byte set, then it is a multicast */
    else if (*packet & 0x01)
    {
        io->ios2_Req.io_Flags |= SANA2IOF_MCAST;
    }

    /* 
        If RAW packet is requested, copy everything, otherwise copy only contents of 
        the frame without ethernet header
    */
    if (io->ios2_Req.io_Flags & SANA2IOF_RAW)
    {
        copyData = packet;
        copyLength = packetLength;
    }
    else
    {
        copyData = packet + 14;
        copyLength = packetLength - 14;
    }

    /* Filter packet if CMD_READ and filter hook is set */
    if (io->ios2_Req.io_Command == CMD_READ && opener->o_FilterHook)
    {
        if (!CallHookPkt(opener->o_FilterHook, io, copyData))
        {
            packetFiltered = TRUE;
        }
    }

    /* Packet not filtered. Send it now and reply request. */
    if (!packetFiltered)
    {
        if (copyLength != 0)
        {
            if (opener->o_RXFunc(io->ios2_Data, copyData, copyLength) == 0)
            {
                io->ios2_WireError = S2WERR_BUFF_ERROR;
                io->ios2_Req.io_Error = S2ERR_NO_RESOURCES;

                /* Report error event */
            }
        }
        else
        {
            D(bug("[WiFi] Received frame without data\n"));
        }

        /* Set number of bytes received */
        io->ios2_DataLength = copyLength;

        Disable();
        Remove((struct Node *)io);
        Enable();
        ReplyMsg((struct Message *)io);
    }
}

void ProcessDataPacket(struct SDIO *sdio, UBYTE *packet, ULONG packetLength)
{
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct WiFiUnit *unit = WiFiBase->w_Unit;
    int accept = TRUE;
    UWORD packetType = *(UWORD*)&packet[12];

    // Get destination address and check if it is a multicast
    uint64_t destAddr = ((uint64_t)*(UWORD*)&packet[0] << 32) |
                        *(ULONG*)&packet[2];
#if 1
    if (packetType == 0x888e)
    {
        struct ExecBase *SysBase = WiFiBase->w_SysBase;
        bug("[DATA.IN] Packet in:\n");
        for (ULONG i=0; i < packetLength; i++)
        {
            if (i % 16 == 0)
                bug("[DATA.IN] %04lx: ", i);
            bug(" %02lx", packet[i]);
            if (i % 16 == 15)
                bug("\n");
        }
        if (packetLength % 16 != 0) bug("\n");
    }
#endif

    if (destAddr != 0xffffffffffffULL && (destAddr & 0x010000000000ULL))
    {
        struct MulticastRange *range;
        accept = FALSE;

        ForeachNode(&unit->wu_MulticastRanges, range)
        {
            if (destAddr >= range->mr_LowerBound && destAddr <= range->mr_UpperBound)
            {
                accept = TRUE;
                break;
            }
        }
    }

    if (accept)
    {
        UBYTE orphan = TRUE;
        struct Opener *opener;

        unit->wu_Stats.PacketsReceived++;

        Disable();
        /* Go through all openers */
        ForeachNode(&unit->wu_Openers, opener)
        {
            struct IOSana2Req *io;
            
            /* Go through all IO read requests pending*/
            ForeachNode(&opener->o_ReadPort.mp_MsgList, io)
            {
                // EthernetII has packet type larger than 1500 (MTU),
                // 802.3 has no packet type but just length
                if (io->ios2_PacketType == packetType ||
                    (packetType <= 1500 && io->ios2_PacketType <= 1500))
                {
                    /* Match, copy packet, break loop for this opener */
                    CopyPacket(io, packet, packetLength);
                    
                    /* The packet is sent at least to one opener, not an orphan anymore */
                    orphan = FALSE;
                    break;
                }
            }
        }
        Enable();

        /* No receiver for this packet found? It's an orphan then */
        if (orphan)
        {
            unit->wu_Stats.UnknownTypesReceived++;

            Disable();
            /* Go through all openers and offer orphan packet to anyone asking */
            ForeachNode(&unit->wu_Openers, opener)
            {
                struct IOSana2Req *io = (APTR)opener->o_OrphanListeners.mp_MsgList.lh_Head;
                /* 
                    If this is a real node, ln_Succ will be not NULL, otherwise it is just 
                    protector node of empty list
                */
                if (io->ios2_Req.io_Message.mn_Node.ln_Succ)
                {
                    CopyPacket(io, packet, packetLength);
                }
            }
            Enable();
        }
    }
}

/*
 * brcmfmac sdio bus specific header
 * This is the lowest layer header wrapped on the packets transmitted between
 * host and WiFi dongle which contains information needed for SDIO core and
 * firmware
 *
 * It consists of 3 parts: hardware header, hardware extension header and
 * software header
 * hardware header (frame tag) - 4 bytes
 * Byte 0~1: Frame length
 * Byte 2~3: Checksum, bit-wise inverse of frame length
 * hardware extension header - 8 bytes
 * Tx glom mode only, N/A for Rx or normal Tx
 * Byte 0~1: Packet length excluding hw frame tag
 * Byte 2: Reserved
 * Byte 3: Frame flags, bit 0: last frame indication
 * Byte 4~5: Reserved
 * Byte 6~7: Tail padding length
 * software header - 8 bytes
 * Byte 0: Rx/Tx sequence number
 * Byte 1: 4 MSB Channel number, 4 LSB arbitrary flag
 * Byte 2: Length of next data frame, reserved for Tx
 * Byte 3: Data offset
 * Byte 4: Flow control bits, reserved for Tx
 * Byte 5: Maximum Sequence number allowed by firmware for Tx, N/A for Rx packet
 * Byte 6~7: Reserved
 */

int SendGlomDataPacket(struct SDIO *sdio, struct IOSana2Req **ioList, UBYTE count)
{
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiUnit *unit = WiFiBase->w_Unit;
    ULONG totalLength = 0;

    struct PacketHeaderHW *pktBase = sdio->s_TXBuffer;
    UBYTE *byteBuffer = sdio->s_TXBuffer;

    for (UBYTE i = 0; i < count; i++)
    {
        struct IOSana2Req *io = ioList[i];
        struct Opener *opener = io->ios2_BufferManagement;
        struct PacketHeaderHW *hw = (APTR)(byteBuffer + totalLength);
        struct GlomHeader *gh = (APTR)((UBYTE*)hw + sizeof(struct PacketHeaderHW));
        struct PacketHeaderSW *hdr = (APTR)((UBYTE*)gh + sizeof(struct GlomHeader));
        
        UWORD packetLength = io->ios2_DataLength + sizeof(struct Packet) + sizeof(struct GlomHeader) + 4;

        if ((io->ios2_Req.io_Flags & SANA2IOF_RAW) == 0)
        {
            packetLength += 14;
        }

        /* Fill HW header */
        hw->ph_Length = LE16(packetLength);
        hw->ph_ChkSum = ~hw->ph_Length;

        /* Fill out glom header */
        gh->gh_Length = LE16(packetLength - 4);
        gh->gh_ReservedB = 0;
        gh->gh_ReservedW = 0;
        if (i == count - 1) gh->gh_LastItem = 1;
        else gh->gh_LastItem = 0;
        gh->gh_TailPad = LE16((-packetLength) & 3);

        /* Following glom header there is PacketSW header */
        hdr->c_ChannelFlag = SDPCM_DATA_CHANNEL;
        hdr->c_DataOffset = sizeof(struct Packet) + sizeof(struct GlomHeader);
        hdr->c_FlowControl = 0;
        hdr->c_Seq = sdio->s_TXSeq++;
        hdr->c_NextLength = 0;
        hdr->c_MaxSeq = 0;
        hdr->c_Reserved[0] = 0;
        hdr->c_Reserved[1] = 0;

        /* Finally packet data */
        UBYTE *ptr = (UBYTE *)hdr + sizeof(struct PacketHeaderSW);

        /* BDC Header */
        *(ULONG *)ptr = 0x20000000;
        ptr += 4;

        if ((io->ios2_Req.io_Flags & SANA2IOF_RAW) == 0)
        {
            // Use the same ugly hack as in case of receiving packets
            APTR src, dst;
            
            // Copy destination
            src = &io->ios2_DstAddr[0];
            dst = &ptr[0];
            ((ULONG*)dst)[0] = ((ULONG*)src)[0];
            ((UWORD*)dst)[2] = ((UWORD*)src)[2];
    
            // Copy source
            src = &unit->wu_EtherAddr[0];
            dst = &ptr[6];
            ((ULONG*)dst)[0] = ((ULONG*)src)[0];
            ((UWORD*)dst)[2] = ((UWORD*)src)[2];

            // Copy packet type
            *(UWORD*)&ptr[12] = io->ios2_PacketType;
            ptr+=14;
        }

        if (io->ios2_DataLength != 0)
        {
            // Copy packet contents
            opener->o_TXFunc(ptr, io->ios2_Data, io->ios2_DataLength);
        }
        else
        {
            D(bug("[WiFi] Sending Frame without data, packet type %04lx\n", io->ios2_PacketType));
        }

#if 1
        if (io->ios2_PacketType == 0x888e)
        {
            UBYTE *ptr = (UBYTE *)hdr + sizeof(struct PacketHeaderSW) + 4;
            ULONG length = packetLength - sizeof(struct Packet) - sizeof(struct GlomHeader) - 4;

            bug("[DATA.OUT] Packet out:\n");
            for (ULONG i=0; i < length; i++)
            {
                if (i % 16 == 0)
                    bug("[DATA.OUT] %04lx: ", i);
                bug(" %02lx", ptr[i]);
                if (i % 16 == 15)
                    bug("\n");
            }
            if (packetLength % 16 != 0) bug("\n");
        }
#endif
        // Increase total length by packet length (aligned)
        totalLength += (packetLength + 3) & ~3;
    }

    pktBase->ph_Length = LE16(totalLength);
    pktBase->ph_ChkSum = ~pktBase->ph_Length;
#if 0
    UBYTE *bdata = (UBYTE*)pktBase;
    for (ULONG i=0; i < totalLength; i++)
    {
        if (i % 16 == 0)
            bug("[GLOM] %04lx:", i);
        bug(" %02lx", bdata[i]);
        if (i % 16 == 15)
            bug("\n");
    }
    if (totalLength % 16 != 0) bug("\n");
#endif
#if 0
    while(1);
#endif
    sdio->SendPKT((UBYTE *)pktBase, totalLength, sdio);

    for (UBYTE i = 0; i < count; i++) {
        ReplyMsg(&ioList[i]->ios2_Req.io_Message);
        unit->wu_Stats.PacketsSent++;
    }

    return 1;
}

int SendDataPacket(struct SDIO *sdio, struct IOSana2Req *io)
{
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiUnit *unit = WiFiBase->w_Unit;
    struct Opener *opener = io->ios2_BufferManagement;
    struct TimerBase *TimerBase = unit->wu_TimerBase;
    ULONG totalLength = 0;
    UBYTE *byteBuffer = sdio->s_TXBuffer;
    struct PacketHeaderHW *pktBase = sdio->s_TXBuffer;

    /* If this is not the first packet in gloom, take it's length now */
    if (unit->wu_GlomCount > 0) {
        totalLength = LE16(pktBase->ph_Length);
    }
    /* New glom packet. Get creation time */
    else {
        GetSysTime(&unit->wu_GlomCreationTime);
    }

    /* Prepare the packet */
    
    struct PacketHeaderHW *hw = (APTR)(byteBuffer + totalLength);
    struct GlomHeader *gh = (APTR)((UBYTE *)hw + sizeof(struct PacketHeaderHW));
    struct PacketHeaderSW *hdr = (APTR)((UBYTE *)gh + sizeof(struct GlomHeader));

    UWORD packetLength = io->ios2_DataLength + sizeof(struct Packet) + sizeof(struct GlomHeader) + 4;

    if ((io->ios2_Req.io_Flags & SANA2IOF_RAW) == 0)
    {
        packetLength += 14;
    }

    /* Fill HW header */
    hw->ph_Length = LE16(packetLength);
    hw->ph_ChkSum = ~hw->ph_Length;

    /* Fill out glom header */
    gh->gh_Length = LE16(packetLength - 4);
    gh->gh_ReservedB = 0;
    gh->gh_ReservedW = 0;
    gh->gh_LastItem = 0;
    unit->wu_GlomLastItemMarker = &gh->gh_LastItem;
    gh->gh_TailPad = LE16((-packetLength) & 3);

    /* Following glom header there is PacketSW header */
    hdr->c_ChannelFlag = SDPCM_DATA_CHANNEL;
    hdr->c_DataOffset = sizeof(struct Packet) + sizeof(struct GlomHeader);
    hdr->c_FlowControl = 0;
    hdr->c_Seq = sdio->s_TXSeq++;
    hdr->c_NextLength = 0;
    hdr->c_MaxSeq = 0;
    hdr->c_Reserved[0] = 0;
    hdr->c_Reserved[1] = 0;

    /* Finally packet data */
    UBYTE *ptr = (UBYTE *)hdr + sizeof(struct PacketHeaderSW);

    /* BDC Header */
    *(ULONG *)ptr = 0x20000000;
    ptr += 4;

    if ((io->ios2_Req.io_Flags & SANA2IOF_RAW) == 0)
    {
        // Use the same ugly hack as in case of receiving packets
        APTR src, dst;
        
        // Copy destination
        src = &io->ios2_DstAddr[0];
        dst = &ptr[0];
        ((ULONG*)dst)[0] = ((ULONG*)src)[0];
        ((UWORD*)dst)[2] = ((UWORD*)src)[2];

        // Copy source
        src = &unit->wu_EtherAddr[0];
        dst = &ptr[6];
        ((ULONG*)dst)[0] = ((ULONG*)src)[0];
        ((UWORD*)dst)[2] = ((UWORD*)src)[2];

        // Copy packet type
        *(UWORD*)&ptr[12] = io->ios2_PacketType;
        ptr+=14;
    }

    if (io->ios2_DataLength != 0)
    {
        // Copy packet contents
        opener->o_TXFunc(ptr, io->ios2_Data, io->ios2_DataLength);
    }
    else
    {
        D(bug("[WiFi] Sending Frame without data, packet type %04lx\n", io->ios2_PacketType));
    }

    // Increase total length by packet length (aligned)
    totalLength += (packetLength + 3) & ~3;

    /* Packet is added now, update its length and gloom count. */
    pktBase->ph_Length = LE16(totalLength);
    pktBase->ph_ChkSum = ~pktBase->ph_Length;

    /* Put request into queue and increase glom count */
    //unit->wu_GlomQueue[unit->wu_GlomCount++] = io;

    ReplyMsg((struct Message *)io);
    unit->wu_Stats.PacketsSent++;

    unit->wu_GlomCount++;

    /* Is glom frame full? Send it out now */
    if (unit->wu_GlomCount == 32) {
        //D(bug("[WiFi.0] Sent completely filled glom packet with count %ld\n", unit->wu_GlomCount));
        *unit->wu_GlomLastItemMarker = 1;
        sdio->SendPKT((UBYTE *)pktBase, totalLength, sdio);
#if 0
        for (unsigned int i=0; i < 32; i++) { 
            ReplyMsg((struct Message *)unit->wu_GlomQueue[i]);
        }
        unit->wu_Stats.PacketsSent+=32;
#endif
        unit->wu_GlomCount = 0;
    }

    return 1;
}

#if 0
void NetworkScanner(struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;

    // Create MessagePort and timer.device IORequest
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *tr = (struct timerequest *)CreateIORequest(port, sizeof(struct timerequest));
    ULONG sigSet;
    ULONG scanDelay = 10;

    if (port == NULL || tr == NULL)
    {
        D(bug("[WiFi.SCAN] Failed to create IO Request\n"));
        if (port != NULL) DeleteMsgPort(port);
        return;
    }

    if (OpenDevice("timer.device", UNIT_VBLANK, &tr->tr_node, 0))
    {
        D(bug("[WiFi.SCAN] Failed to open timer.device\n"));
        DeleteIORequest(&tr->tr_node);
        DeleteMsgPort(port);
        return;
    }

    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_time.tv_sec = 1;
    tr->tr_time.tv_micro = 0;
    SendIO(&tr->tr_node);

    do {
        sigSet = Wait(SIGBREAKF_CTRL_C | (1 << port->mp_SigBit));
        
        if (sigSet & (1 << port->mp_SigBit))
        {
            if (CheckIO(&tr->tr_node))
            {
                WaitIO(&tr->tr_node);
            }

            // No network is in progress, decrease delay and start scanner
            if (!sdio->s_WiFiBase->w_NetworkScanInProgress)
            {
                if (scanDelay == 10)
                {
                    struct WiFiNetwork *network, *next;
                    ObtainSemaphore(&sdio->s_WiFiBase->w_NetworkListLock);
                    D(bug("[WiFI] Network list from scanner:\n"));
                    ForeachNodeSafe(&sdio->s_WiFiBase->w_NetworkList, network, next)
                    {
                        // If network wasn't available for more than 60 seconds after scan, remove it
                        if (network->wn_LastUpdated++ > 6) {
                            Remove((struct Node*)network);
                            if (network->wn_IE)
                                FreePooled(WiFiBase->w_MemPool, network->wn_IE, network->wn_IELength);
                            FreePooled(WiFiBase->w_MemPool, network, sizeof(struct WiFiNetwork));
                        }
                        else
                        {
                            D(bug("[WiFi]   SSID: '%-32s', BSID: %02lx:%02lx:%02lx:%02lx:%02lx:%02lx, Type: %s, Channel %ld, Spec:%04lx, RSSI: %ld\n",
                                (ULONG)network->wn_SSID, network->wn_BSID[0], network->wn_BSID[1], network->wn_BSID[2],
                                network->wn_BSID[3], network->wn_BSID[4], network->wn_BSID[5], 
                                network->wn_ChannelInfo.ci_Band == BRCMU_CHAN_BAND_2G ? (ULONG)"2.4GHz" : (ULONG)"5GHz",
                                network->wn_ChannelInfo.ci_CHNum, network->wn_ChannelInfo.ci_CHSpec,
                                network->wn_RSSI
                            ));
                        }
                    }
                    ReleaseSemaphore(&sdio->s_WiFiBase->w_NetworkListLock);
                }

                if (scanDelay) scanDelay--;

                if (scanDelay == 0)
                {
                    StartNetworkScan(sdio);
                }
            }
            else
            {
                scanDelay = 10;
            }

            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_sec = 1;
            tr->tr_time.tv_micro = 0;
            SendIO(&tr->tr_node);
        }

    } while(!(sigSet & SIGBREAKF_CTRL_C));

    CloseDevice(&tr->tr_node);
    DeleteIORequest(&tr->tr_node);
    DeleteMsgPort(port);
}
#endif
static const char * const brcmf_fil_errstr[] = {
    "BCME_OK",
    "BCME_ERROR",
    "BCME_BADARG",
    "BCME_BADOPTION",
    "BCME_NOTUP",
    "BCME_NOTDOWN",
    "BCME_NOTAP",
    "BCME_NOTSTA",
    "BCME_BADKEYIDX",
    "BCME_RADIOOFF",
    "BCME_NOTBANDLOCKED",
    "BCME_NOCLK",
    "BCME_BADRATESET",
    "BCME_BADBAND",
    "BCME_BUFTOOSHORT",
    "BCME_BUFTOOLONG",
    "BCME_BUSY",
    "BCME_NOTASSOCIATED",
    "BCME_BADSSIDLEN",
    "BCME_OUTOFRANGECHAN",
    "BCME_BADCHAN",
    "BCME_BADADDR",
    "BCME_NORESOURCE",
    "BCME_UNSUPPORTED",
    "BCME_BADLEN",
    "BCME_NOTREADY",
    "BCME_EPERM",
    "BCME_NOMEM",
    "BCME_ASSOCIATED",
    "BCME_RANGE",
    "BCME_NOTFOUND",
    "BCME_WME_NOT_ENABLED",
    "BCME_TSPEC_NOTFOUND",
    "BCME_ACM_NOTSUPPORTED",
    "BCME_NOT_WME_ASSOCIATION",
    "BCME_SDIO_ERROR",
    "BCME_DONGLE_DOWN",
    "BCME_VERSION",
    "BCME_TXFAIL",
    "BCME_RXFAIL",
    "BCME_NODEVICE",
    "BCME_NMODE_DISABLED",
    "BCME_NONRESIDENT",
    "BCME_SCANREJECT",
    "BCME_USAGE_ERROR",
    "BCME_IOCTL_ERROR",
    "BCME_SERIAL_PORT_ERR",
    "BCME_DISABLED",
    "BCME_DECERR",
    "BCME_ENCERR",
    "BCME_MICERR",
    "BCME_REPLAY",
    "BCME_IE_NOTFOUND",
};

void PacketDump(struct SDIO *sdio, APTR data, char *src)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct Packet *pkt = data;
    ULONG dataLength = LE16(pkt->p_Length);// - sizeof(struct Packet);

    D(bug("[%s] Packet dump: \n", (ULONG)src));
    D(bug("[%s]   Len=%04lx, ChkSum=%04lx\n", (ULONG)src, LE16(pkt->p_Length), LE16(pkt->c_ChkSum)));
    D(bug("[%s]   SEQ=%03ld, CHAN=%ld, NEXT=%ld, DATA=%ld, FLOW=%ld, MAX_SEQ=%ld\n", (ULONG)src, 
                        pkt->c_Seq, pkt->c_ChannelFlag, pkt->c_NextLength, pkt->c_DataOffset, pkt->c_FlowControl, pkt->c_MaxSeq));
    
    data = (APTR)((ULONG)data + pkt->c_DataOffset);

    if (pkt->c_ChannelFlag == 0)
    {
        struct PacketCmd *c = data;
        D(bug("[%s]   CMD=%08lx, FLAGS=%04lx, ID=%04lx, STAT=%08lx\n", (ULONG)src, LE32(c->c_Command), LE16(c->c_Flags),
            LE16(c->c_ID), LE32(c->c_Status)));
    
        if (LE16(c->c_Flags) & BCDC_DCMD_ERROR)
        {
            LONG errCode = LE32(c->c_Status);
            D(bug("[%s]   Command ended with error: %s\n", (ULONG)src, (ULONG)brcmf_fil_errstr[-errCode]));
        }

        data = (APTR)((ULONG)data + sizeof(struct PacketCmd));
        //dataLength -= sizeof(struct PacketCmd);
    }
    
    UBYTE *bdata = (UBYTE*)pkt;

    if (pkt->c_ChannelFlag != 3)
        if (dataLength > 64) dataLength = 64;

    for (ULONG i=0; i < dataLength; i++)
    {
        if (i % 16 == 0)
            bug("[%s]  ", (ULONG)src);
        bug(" %02lx", bdata[i]);
        if (i % 16 == 15)
            bug("\n");
    }
    if (dataLength % 16 != 0) bug("\n");
}

static int int_strlen(const char *c)
{
    int len = 0;
    if (!c) return 0;

    while(*c++) len++;

    return len;
}

int PacketSetVar(struct SDIO *sdio, char *varName, const void *setBuffer, int setSize)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    UBYTE *pkt;
    struct MsgPort *port = CreateMsgPort();
    struct PacketMessage *mpkt;
    ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + sizeof(struct PacketMessage) + setSize;
    ULONG error_code = 0;

    if (sdio->s_GlomEnabled)
        totalLen += 8;

    int varSize = int_strlen(varName) + 1;

    totalLen += varSize;

    mpkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);
    pkt = (APTR)&mpkt->pm_PacketHeader[0];

    mpkt->pm_Message.mn_ReplyPort = port;
    mpkt->pm_Message.mn_Length = totalLen;

    struct PacketHeaderHW *hw = (APTR)&pkt[0];
    struct GlomHeader *gl = (APTR)&pkt[4];
    struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
    struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];
    
    mpkt->pm_PacketData = c;
    UWORD totLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + varSize + setSize;
    
    if (sdio->s_GlomEnabled)
    {
        totLen += 8;
        gl->gh_Length = LE16(totLen - sizeof(struct PacketHeaderHW));
        gl->gh_ReservedB = 0;
        gl->gh_LastItem = 1;
        gl->gh_ReservedW = 0;
        gl->gh_TailPad = LE16((-totLen) & 3);
    }

    hw->ph_Length = LE16(totLen);
    hw->ph_ChkSum = ~hw->ph_Length;
    sw->c_DataOffset = sizeof(struct Packet);
    if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
    sw->c_FlowControl = 0;
    sw->c_Seq = sdio->s_TXSeq++;

    c->c_Command = LE32(BRCMF_C_SET_VAR); 
    c->c_Length = LE32(varSize + setSize);
    c->c_Flags = LE16(BCDC_DCMD_SET);
    c->c_ID = LE16(++(sdio->s_CmdID));
    c->c_Status = 0;

    UBYTE *param = (UBYTE*)c + sizeof(struct PacketCmd);
    
    CopyMem(varName, &param[0], varSize);
    CopyMem((APTR)setBuffer, &param[varSize], setSize);

    PutMsg(sdio->s_ReceiverPort, &mpkt->pm_Message);
    WaitPort(port);
    GetMsg(port);

    if (c->c_Flags & LE16(BCDC_DCMD_ERROR))
    {
        error_code = LE32(c->c_Status);
        D(bug("[WiFi] PacketSetVar ended with error. Code: %s", (ULONG)brcmf_fil_errstr[-error_code]));
    }

    FreePooled(WiFiBase->w_MemPool, mpkt, totalLen);
    DeleteMsgPort(port);

    return error_code;
}

void PacketSetVarAsync(struct SDIO *sdio, char *varName, const void *setBuffer, int setSize)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    UBYTE *pkt;
    ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + setSize;

    if (sdio->s_GlomEnabled)
        totalLen += 8;

    int varSize = int_strlen(varName) + 1;

    totalLen += varSize;

    pkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);

    struct PacketHeaderHW *hw = (APTR)&pkt[0];
    struct GlomHeader *gl = (APTR)&pkt[4];
    struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
    struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];

    if (sdio->s_GlomEnabled)
    {
        gl->gh_Length = LE16(totalLen - sizeof(struct PacketHeaderHW));
        gl->gh_ReservedB = 0;
        gl->gh_LastItem = 1;
        gl->gh_ReservedW = 0;
        gl->gh_TailPad = LE16((-totalLen) & 3);
    }

    hw->ph_Length = LE16(totalLen);
    hw->ph_ChkSum = ~hw->ph_Length;
    sw->c_DataOffset = sizeof(struct Packet);
    if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
    sw->c_FlowControl = 0;
    sw->c_Seq = sdio->s_TXSeq++;

    c->c_Command = LE32(263);
    c->c_Length = LE32(varSize + setSize);
    c->c_Flags = LE16(BCDC_DCMD_SET);
    c->c_ID = LE16(++(sdio->s_CmdID));
    c->c_Status = 0;

    UBYTE *param = (UBYTE*)c + sizeof(struct PacketCmd);
    
    CopyMem(varName, &param[0], varSize);
    CopyMem((APTR)setBuffer, &param[varSize], setSize);

    // Async - fire the packet and forget
    sdio->SendPKT(pkt, totalLen, sdio);

    FreePooled(WiFiBase->w_MemPool, pkt, totalLen);
}

int PacketSetVarInt(struct SDIO *sdio, char *varName, ULONG varValue)
{
    ULONG val = LE32(varValue);
    return PacketSetVar(sdio, varName, &val, 4);
}

void PacketSetVarIntAsync(struct SDIO *sdio, char *varName, ULONG varValue)
{
    ULONG val = LE32(varValue);
    PacketSetVarAsync(sdio, varName, &val, 4);
}

int PacketCmdInt(struct SDIO *sdio, ULONG cmd, ULONG cmdValue)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    UBYTE *pkt;
    struct MsgPort *port = CreateMsgPort();
    struct PacketMessage *mpkt;
    ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + sizeof(struct PacketMessage) + 4;
    ULONG error_code = 0;

    if (sdio->s_GlomEnabled)
        totalLen += 8;

    mpkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);
    pkt = (APTR)&mpkt->pm_PacketHeader[0];

    mpkt->pm_Message.mn_ReplyPort = port;
    mpkt->pm_Message.mn_Length = totalLen;
    
    struct PacketHeaderHW *hw = (APTR)&pkt[0];
    struct GlomHeader *gl = (APTR)&pkt[4];
    struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
    struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];

    mpkt->pm_PacketData = c;

    UWORD totLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + 4;
    
    if (sdio->s_GlomEnabled)
    {
        totLen += 8;
        gl->gh_Length = LE16(totLen - sizeof(struct PacketHeaderHW));
        gl->gh_ReservedB = 0;
        gl->gh_LastItem = 1;
        gl->gh_ReservedW = 0;
        gl->gh_TailPad = LE16((-totLen) & 3);
    }

    hw->ph_Length = LE16(totLen);
    hw->ph_ChkSum = ~hw->ph_Length;
    sw->c_DataOffset = sizeof(struct Packet);
    if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
    sw->c_FlowControl = 0;
    sw->c_Seq = sdio->s_TXSeq++;

    c->c_Command = LE32(cmd);
    c->c_Length = LE32(4);
    c->c_Flags = LE16(BCDC_DCMD_SET);
    c->c_ID = LE16(++(sdio->s_CmdID));
    c->c_Status = 0;

    ULONG *param = (APTR)((UBYTE*)c + sizeof(struct PacketCmd));

    *param = LE32(cmdValue);

    PutMsg(sdio->s_ReceiverPort, &mpkt->pm_Message);
    WaitPort(port);
    GetMsg(port);

    if (c->c_Flags & LE16(BCDC_DCMD_ERROR))
    {
        error_code = LE32(c->c_Status);
        D(bug("[WiFi] PacketCmdInt ended with error. Code: %s", (ULONG)brcmf_fil_errstr[-error_code]));
    }

    FreePooled(WiFiBase->w_MemPool, mpkt, totalLen);
    DeleteMsgPort(port);

    return error_code;
}

void PacketCmdIntAsync(struct SDIO *sdio, ULONG cmd, ULONG cmdValue)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    UBYTE *pkt;
    ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + 4;

    if (sdio->s_GlomEnabled)
        totalLen += 8;

    pkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);
    
    struct PacketHeaderHW *hw = (APTR)&pkt[0];
    struct GlomHeader *gl = (APTR)&pkt[4];
    struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
    struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];

    if (sdio->s_GlomEnabled)
    {
        gl->gh_Length = LE16(totalLen - sizeof(struct PacketHeaderHW));
        gl->gh_ReservedB = 0;
        gl->gh_LastItem = 1;
        gl->gh_ReservedW = 0;
        gl->gh_TailPad = LE16((-totalLen) & 3);
    }

    hw->ph_Length = LE16(totalLen);
    hw->ph_ChkSum = ~hw->ph_Length;
    sw->c_DataOffset = sizeof(struct Packet);
    if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
    sw->c_FlowControl = 0;
    sw->c_Seq = sdio->s_TXSeq++;

    c->c_Command = LE32(cmd);
    c->c_Length = LE32(4);
    c->c_Flags = LE16(BCDC_DCMD_SET);
    c->c_ID = LE16(++(sdio->s_CmdID));
    c->c_Status = 0;

    ULONG *param = (APTR)((UBYTE*)c + sizeof(struct PacketCmd));

    // Put command argument into packet
    *param = LE32(cmdValue);

    // Fire packet and forget it
    sdio->SendPKT(pkt, totalLen, sdio);

    FreePooled(WiFiBase->w_MemPool, pkt, totalLen);
}

int PacketCmdIntGet(struct SDIO *sdio, ULONG cmd, ULONG *cmdValue)
{
    ULONG error_code = 2;

    if (cmdValue != NULL)
    {
        struct ExecBase *SysBase = sdio->s_SysBase;
        struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
        UBYTE *pkt;
        struct MsgPort *port = CreateMsgPort();
        struct PacketMessage *mpkt;
        ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + sizeof(struct PacketMessage) + 4;
        error_code = 0;

        if (sdio->s_GlomEnabled)
            totalLen += 8;

        mpkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);
        pkt = (APTR)&mpkt->pm_PacketHeader[0];

        mpkt->pm_Message.mn_ReplyPort = port;
        mpkt->pm_Message.mn_Length = totalLen;
        mpkt->pm_RecvBuffer = cmdValue;
        mpkt->pm_RecvSize = 4;
        
        struct PacketHeaderHW *hw = (APTR)&pkt[0];
        struct GlomHeader *gl = (APTR)&pkt[4];
        struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
        struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];

        mpkt->pm_PacketData = c;

        UWORD totLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + 4;
        
        if (sdio->s_GlomEnabled)
        {
            totLen += 8;
            gl->gh_Length = LE16(totLen - sizeof(struct PacketHeaderHW));
            gl->gh_ReservedB = 0;
            gl->gh_LastItem = 1;
            gl->gh_ReservedW = 0;
            gl->gh_TailPad = LE16((-totLen) & 3);
        }

        hw->ph_Length = LE16(totLen);
        hw->ph_ChkSum = ~hw->ph_Length;
        sw->c_DataOffset = sizeof(struct Packet);
        if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
        sw->c_FlowControl = 0;
        sw->c_Seq = sdio->s_TXSeq++;

        c->c_Command = LE32(cmd);
        c->c_Length = LE32(4);
        c->c_Flags = LE16(0);
        c->c_ID = LE16(++(sdio->s_CmdID));
        c->c_Status = 0;

        //PacketDump(sdio, p, "WiFi");

        PutMsg(sdio->s_ReceiverPort, &mpkt->pm_Message);
        WaitPort(port);
        GetMsg(port);

        if (c->c_Flags & LE16(BCDC_DCMD_ERROR))
        {
            error_code = LE32(c->c_Status);
            D(bug("[WiFi] PacketCmdIntGet ended with error. Code: %s", (ULONG)brcmf_fil_errstr[-error_code]));
        }
        else
        {
            *cmdValue = LE32(*cmdValue);
        }

        FreePooled(WiFiBase->w_MemPool, mpkt, totalLen);
        DeleteMsgPort(port);
    }

    return error_code;
}

int PacketGetVar(struct SDIO *sdio, char *varName, void *getBuffer, int getSize)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    UBYTE *pkt;
    struct MsgPort *port = CreateMsgPort();
    struct PacketMessage *mpkt;
    ULONG totalLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + sizeof(struct PacketMessage);
    ULONG error_code = 0;

    if (sdio->s_GlomEnabled)
        totalLen += 8;

    int varSize = int_strlen(varName) + 1;

    if (varSize > getSize)
        totalLen += varSize;
    else
        totalLen += getSize;

    mpkt = AllocPooledClear(WiFiBase->w_MemPool, totalLen);
    pkt = (APTR)&mpkt->pm_PacketHeader[0];

    mpkt->pm_Message.mn_ReplyPort = port;
    mpkt->pm_Message.mn_Length = totalLen;
    mpkt->pm_RecvBuffer = getBuffer;
    mpkt->pm_RecvSize = getSize;

    struct PacketHeaderHW *hw = (APTR)&pkt[0];
    struct GlomHeader *gl = (APTR)&pkt[4];
    struct PacketHeaderSW *sw = sdio->s_GlomEnabled ? (APTR)&pkt[12] : (APTR)&pkt[4];
    struct PacketCmd *c = sdio->s_GlomEnabled ? (APTR)&pkt[20] : (APTR)&pkt[12];

    mpkt->pm_PacketData = c;

    UWORD max = varSize;
    if (getSize > max) max = getSize;

    UWORD totLen = sizeof(struct Packet) + sizeof(struct PacketCmd) + max;
    
    if (sdio->s_GlomEnabled)
    {
        totLen += 8;
        gl->gh_Length = LE16(totLen - sizeof(struct PacketHeaderHW));
        gl->gh_ReservedB = 0;
        gl->gh_LastItem = 1;
        gl->gh_ReservedW = 0;
        gl->gh_TailPad = LE16((-totLen) & 3);
    }

    hw->ph_Length = LE16(totLen);
    hw->ph_ChkSum = ~hw->ph_Length;
    sw->c_DataOffset = sizeof(struct Packet);
    if (sdio->s_GlomEnabled) sw->c_DataOffset += sizeof(struct GlomHeader);
    sw->c_FlowControl = 0;
    sw->c_Seq = sdio->s_TXSeq++;

    c->c_Command = LE32(262);
    c->c_Length = LE32(max);
    c->c_Flags = LE16(0);
    c->c_ID = LE16(++(sdio->s_CmdID));
    c->c_Status = 0;

    UBYTE *param = (UBYTE*)c + sizeof(struct PacketCmd);
    
    CopyMem(varName, &param[0], varSize);

    PutMsg(sdio->s_ReceiverPort, &mpkt->pm_Message);
    WaitPort(port);
    GetMsg(port);

    if (c->c_Flags & LE16(BCDC_DCMD_ERROR))
    {
        error_code = LE32(c->c_Status);
        D(bug("[WiFi] PacketGetVar ended with error. Code: %s", (ULONG)brcmf_fil_errstr[-error_code]));
    }

    FreePooled(WiFiBase->w_MemPool, mpkt, totalLen);
    DeleteMsgPort(port);

    return error_code;
}

#define MAX_CHUNK_LEN			1400

#define DLOAD_HANDLER_VER		1	/* Downloader version */
#define DLOAD_FLAG_VER_MASK		0xf000	/* Downloader version mask */
#define DLOAD_FLAG_VER_SHIFT		12	/* Downloader version shift */

#define DL_BEGIN			0x0002
#define DL_END				0x0004

#define DL_TYPE_CLM			2

int PacketUploadCLM(struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;

    // Check if there is CLM to be uploaded
    if (sdio->s_Chip->c_CLMBase && sdio->s_Chip->c_CLMSize)
    {
        LONG dataLen = sdio->s_Chip->c_CLMSize;
        ULONG transferred = 0;
        UBYTE *data = sdio->s_Chip->c_CLMBase;
        UWORD flag = DL_BEGIN | (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT);

        struct UploadHeader {
            UWORD flag;
            UWORD dload_type;
            ULONG len;
            ULONG crc;
            UBYTE data[];
        };

        struct UploadHeader *upload = AllocPooled(WiFiBase->w_MemPool, sizeof(struct UploadHeader) + MAX_CHUNK_LEN);

        if (upload)
        {
            // Upload CLM in chunks of size MAX_CHUNK_LEN
            do {
                ULONG transferLen; 

                if (dataLen > MAX_CHUNK_LEN) {
                    transferLen = MAX_CHUNK_LEN;
                }
                else {
                    transferLen = dataLen;
                    flag |= DL_END;
                }

                CopyMem(data, &upload->data[0], transferLen);

                upload->flag = LE16(flag);
                upload->dload_type = LE16(DL_TYPE_CLM);
                upload->len = LE32(transferLen);
                upload->crc = 0;

                PacketSetVar(sdio, "clmload", upload, sizeof(struct UploadHeader) + transferLen);

                transferred += transferLen;
                dataLen -= transferLen;
                data += transferLen;

                flag &= ~DL_BEGIN;
            } while (dataLen > 0);

            FreePooled(WiFiBase->w_MemPool, upload, sizeof(struct UploadHeader) + MAX_CHUNK_LEN);
        }

        //D(bug("[WiFi] CLM upload complete. Getting status\n"));
        //PacketGetVar(sdio, "clmload_status", NULL, 32);
    }
    else
    {
        D(bug("[WiFi] No CLM to upload\n"));
    }

    return 1;
}

void StartNetworkScan(struct IOSana2Req *io)
{
    struct WiFiUnit *unit = (APTR)io->ios2_Req.io_Unit;
    struct WiFiBase *base = unit->wu_Base;
    struct ExecBase *SysBase = base->w_SysBase;
    struct Library *UtilityBase = base->w_UtilityBase;
    struct SDIO *sdio = base->w_SDIO;
    UBYTE *networkName = NULL;
    struct TagItem *tags = io->ios2_StatData;

    /* THis needs to be gone! The paramsv2 layout is known... */
    static const UBYTE params[4+2+2+4+32+6+1+1+4*4+2+2+14*2+32+4] = {
        1,0,0,0,
        1,0,
        0x34,0x12,
        0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0xff,0xff,0xff,0xff,0xff,0xff,
        2,
        0,
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,
        14,0,
        0,0,
        0x01,0x2b,0x02,0x2b,0x03,0x2b,0x04,0x2b,0x05,0x2e,0x06,0x2e,0x07,0x2e,
        0x08,0x2b,0x09,0x2b,0x0a,0x2b,0x0b,0x2b,0x0c,0x2b,0x0d,0x2b,0x0e,0x2b,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    };
    UBYTE *data = (UBYTE*)params;
    
    D(bug("[WiFi] StartNetworkScan("));

    /* If tags were passed check if S2INFO_SSID was set */
    if (tags != NULL) {
        networkName = (UBYTE*)GetTagData(S2INFO_SSID, 0, tags);
    }

    if (networkName) D(bug(networkName));
    
    D(bug(")\n"));

    if (networkName)
    {
        ULONG len = _strlen(networkName);
        data = AllocVecPooled(base->w_MemPool, sizeof(params));

        if (len > 32) len = 32;

        CopyMem((const APTR)params, data, sizeof(params));
        
        data[8] = len;
        for (int i=0; i < data[8]; i++)
            data[12 + i] = networkName[i];
    }

    io->ios2_DataLength = 0;
    io->ios2_StatData = NULL;

    unit->wu_ScanRequest = io;

    PacketCmdIntAsync(sdio, BRCMF_C_SET_PASSIVE_SCAN, 0);
    PacketSetVarAsync(sdio, "escan", data, sizeof(params));
    
    if(networkName)
        FreeVecPooled(base->w_MemPool, data);
}

#if 0
static void StartScannerTask(struct SDIO *sdio)
{
    struct WiFiBase *WiFiBase = sdio->s_WiFiBase;
    struct ExecBase *SysBase = sdio->s_SysBase;
    APTR entry = (APTR)NetworkScanner;
    struct Task *task;
    struct MemList *ml;
    ULONG *stack;

    static const char task_name[] = "WiFiPi Network Scanner";
    D(bug("[WiFi] Starting network scanner\n"));

    // Get all memory we need for the receiver task
    task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
    ml = AllocMem(sizeof(struct MemList) + sizeof(struct MemEntry), MEMF_PUBLIC | MEMF_CLEAR);
    stack = AllocMem(SCANNER_STACKSIZE * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);

    // Prepare mem list, put task and its stack there
    ml->ml_NumEntries = 2;
    ml->ml_ME[0].me_Un.meu_Addr = task;
    ml->ml_ME[0].me_Length = sizeof(struct Task);

    ml->ml_ME[1].me_Un.meu_Addr = &stack[0];
    ml->ml_ME[1].me_Length = SCANNER_STACKSIZE * sizeof(ULONG);

    // Task's UserData will contain pointer to SDIO
    task->tc_UserData = sdio;

    // Set up stack
    task->tc_SPLower = &stack[0];
    task->tc_SPUpper = &stack[SCANNER_STACKSIZE];

    // Push ThisTask and SDIO on the stack
    stack = (ULONG *)task->tc_SPUpper;
    *--stack = (ULONG)sdio;
    task->tc_SPReg = stack;

    task->tc_Node.ln_Name = (char *)task_name;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Pri = SCANNER_PRIORITY;

    NewMinList((struct MinList *)&task->tc_MemEntry);
    AddHead(&task->tc_MemEntry, &ml->ml_Node);

    D(bug("[WiFi] Bringing scanner to life\n"));

    sdio->s_ScannerTask = AddTask(task, entry, NULL);
}
#endif
void StartPacketReceiver(struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    APTR entry = (APTR)PacketReceiver;
    struct Task *task;
    struct MemList *ml;
    ULONG *stack;
    static const char task_name[] = "WiFiPi Packet Receiver";

    D(bug("[WiFi] Starting packet receiver task\n"));

    // Get all memory we need for the receiver task
    task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
    ml = AllocMem(sizeof(struct MemList) + sizeof(struct MemEntry), MEMF_PUBLIC | MEMF_CLEAR);
    stack = AllocMem(PACKET_RECV_STACKSIZE * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);

    // Prepare mem list, put task and its stack there
    ml->ml_NumEntries = 2;
    ml->ml_ME[0].me_Un.meu_Addr = task;
    ml->ml_ME[0].me_Length = sizeof(struct Task);

    ml->ml_ME[1].me_Un.meu_Addr = &stack[0];
    ml->ml_ME[1].me_Length = PACKET_RECV_STACKSIZE * sizeof(ULONG);

    // Task's UserData will contain pointer to SDIO
    task->tc_UserData = sdio;

    // Set up stack
    task->tc_SPLower = &stack[0];
    task->tc_SPUpper = &stack[PACKET_RECV_STACKSIZE];

    // Push ThisTask and SDIO on the stack
    stack = (ULONG *)task->tc_SPUpper;
    *--stack = (ULONG)FindTask(NULL);
    *--stack = (ULONG)sdio;
    task->tc_SPReg = stack;

    task->tc_Node.ln_Name = (char*)task_name;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Pri = PACKET_RECV_PRIORITY;

    NewMinList((struct MinList *)&task->tc_MemEntry);
    AddHead(&task->tc_MemEntry, &ml->ml_Node);

    D(bug("[WiFi] Bringing packet receiver to life\n"));

    AddTask(task, entry, NULL);
    Wait(SIGBREAKF_CTRL_C);

    //StartScannerTask(sdio);

    if (sdio->s_ReceiverTask)
        D(bug("[WiFi] Packet receiver up and running\n"));
    else
        D(bug("[WiFi] Packet receiver not started!\n"));

#if 0
    UBYTE s_HWAddr[6];
    PacketGetVar(sdio, "cur_etheraddr", s_HWAddr, 6);

    D(bug("[WiFi] Ethernet addr: %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
        s_HWAddr[0], s_HWAddr[1], s_HWAddr[2],
        s_HWAddr[3], s_HWAddr[4], s_HWAddr[5]));

    ULONG d11Type = 0;
    static const char * const types[]= { "UNKNOWN", "N", "AC" };
    if (0 == PacketCmdIntGet(sdio, BRCMF_C_GET_VERSION, &d11Type))
    {
        D(bug("[WiFi] D11 Version: %s\n", (ULONG)types[d11Type]));
        sdio->s_Chip->c_D11Type = d11Type;
    }

    PacketUploadCLM(sdio);

    PacketSetVarInt(sdio, "assoc_listen", 10);

    if (sdio->s_Chip->c_ChipID == BRCM_CC_43430_CHIP_ID || sdio->s_Chip->c_ChipID == BRCM_CC_4345_CHIP_ID)
    {
        PacketCmdInt(sdio, 0x56, 0);
    }
    else
    {
        PacketCmdInt(sdio, 0x56, 2);
    }

    PacketSetVarInt(sdio, "bus:txglom", 0);
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
    D(bug("[WiFi] Firmware version: %s\n", (ULONG)ver));

    PacketSetVarInt(sdio, "roam_off", 1);

    PacketCmdInt(sdio, BRCMF_C_SET_INFRA, 1);
    PacketCmdInt(sdio, BRCMF_C_SET_PROMISC, 0);
    PacketCmdInt(sdio, BRCMF_C_UP, 1);

    StartNetworkScan(sdio);

void delay_us(ULONG us, struct WiFiBase *WiFiBase)
{
    (void)WiFiBase;
    ULONG timer = LE32(*(volatile ULONG*)0xf2003004);
    ULONG end = timer + us;

    if (end < timer) {
        while (end < LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
    }
    while (end > LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
}
delay_us(5000000, sdio->s_WiFiBase);

    ObtainSemaphore(&sdio->s_WiFiBase->w_NetworkListLock);
    struct WiFiNetwork *network;
    UBYTE found = 0;
    ForeachNode(&sdio->s_WiFiBase->w_NetworkList, network)
    {
        if (_strncmp("pistorm", network->wn_SSID, 32) == 0)
        {
            found = 1;
            Connect(sdio, network);
        }
    }
    ReleaseSemaphore(&sdio->s_WiFiBase->w_NetworkListLock);

    if (!found)
    {
        Connect(sdio, NULL);
    }
#endif
#if 0

delay_us(5000000, sdio->s_WiFiBase);
    PacketCmdInt(sdio, 49, 0);
    PacketSetVar(sdio, "escan", params, sizeof(params));


delay_us(5000000, sdio->s_WiFiBase);
    PacketCmdInt(sdio, 49, 0);
    PacketSetVar(sdio, "escan", params, sizeof(params));
#endif

#if 0

    UBYTE pkt[256];
    for (int i=0; i < 256; i++) pkt[i] = 0;
    struct Packet *p = (struct Packet *)&pkt[0];
    struct PacketCmd *c = (struct PacketCmd *)&pkt[12];
    char cmd[] = "cur_etheraddr";
    

    p->p_Length = LE16(12 + 16 + 32); //sizeof(cmd));
    p->c_ChkSum = ~p->p_Length;
    p->c_DataOffset = sizeof(struct Packet);
    p->c_FlowControl = 0;
    p->c_Seq = 0;
    c->c_Command = LE32(262);   // GetVar
    c->c_Length = LE32(32); //sizeof(cmd));     // Length
    c->c_Flags = 0;
    c->c_ID = LE16(1);
    c->c_Status = 0;
    
    CopyMem(cmd, &pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)], sizeof(cmd));
    D(bug("[WiFi] Packet: \n"));
    for (int i=0; i < LE16(p->p_Length); i++)
    {
        if (i % 16 == 0)
            bug("[WiFi]  ");
        bug(" %02lx", pkt[i]);
        if (i % 16 == 15)
            bug("\n");
    }
    if (LE16(p->p_Length) % 16 != 0) bug("\n");

    sdio->SendPKT(pkt, LE16(p->p_Length), sdio);

#endif

#if 0

    for (int i=0; i < 256; i++) pkt[i] = 0;
    p->p_Length = LE16(12 + 16 + 4);
    p->c_ChkSum = ~p->p_Length;
    p->c_DataOffset = sizeof(struct Packet);
    p->c_FlowControl = 0;
    p->c_Seq = 1;
    c->c_Command = LE32(49);   // GetVar
    c->c_Length = LE32(4);     // Length
    c->c_Flags = LE16(2);
    c->c_ID = LE16(2);
    c->c_Status = 0;

    pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)] = 0;
    pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)+1] = 0;
    pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)+2] = 0;
    pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)+3] = 0;

    D(bug("[WiFi] Packet: \n"));
    for (int i=0; i < LE16(p->p_Length); i++)
    {
        if (i % 16 == 0)
            bug("[WiFi]  ");
        bug(" %02lx", pkt[i]);
        if (i % 16 == 15)
            bug("\n");
    }
    if (LE16(p->p_Length) % 16 != 0) bug("\n");

    sdio->SendPKT(pkt, LE16(p->p_Length), sdio);

    for (int i=0; i < 256; i++) pkt[i] = 0;

    p->p_Length = LE16(12 + 16 + sizeof(cmd2) + sizeof(params));
    p->c_ChkSum = ~p->p_Length;
    p->c_DataOffset = sizeof(struct Packet);
    p->c_FlowControl = 0;
    p->c_Seq = 2;
    c->c_Command = LE32(263);   // SetVar
    c->c_Length = LE32(sizeof(cmd2) + sizeof(params));     // Length
    c->c_Flags = LE16(2);
    c->c_ID = LE16(3);
    c->c_Status = 0;
    
    CopyMem(cmd2, &pkt[sizeof(struct Packet) + sizeof(struct PacketCmd)], sizeof(cmd2));
    CopyMem(params, &pkt[sizeof(struct Packet) + sizeof(struct PacketCmd) + sizeof(cmd2)], sizeof(params));
    
    D(bug("[WiFi] Packet: \n"));
    for (int i=0; i < LE16(p->p_Length); i++)
    {
        if (i % 16 == 0)
            bug("[WiFi]  ");
        bug(" %02lx", pkt[i]);
        if (i % 16 == 15)
            bug("\n");
    }
    if (LE16(p->p_Length) % 16 != 0) bug("\n");

    sdio->SendPKT(pkt, LE16(p->p_Length), sdio);

    #endif
}
