#ifndef _EMU68_VC4_H
#define _EMU68_VC4_H

#include <exec/types.h>
#include <exec/libraries.h>

#include <dos/dos.h>
#include <intuition/intuitionbase.h>
#include <libraries/expansionbase.h>
#include <stdint.h>

#include "boardinfo.h"

#define STR(s) #s
#define XSTR(s) STR(s)

#define VC4CARD_VERSION  1
#define VC4CARD_REVISION 0
#define VC4CARD_PRIORITY 0
#define MBOX_SIZE        (512 * 4)

#define CLOCK_HZ        25000000

struct Size {
    UWORD width;
    UWORD height;
};

struct VC4Base {
    struct CardBase         vc4_LibNode;
    BPTR                    vc4_SegList;
    struct ExecBase *       vc4_SysBase;
    struct ExpansionBase *  vc4_ExpansionBase;
    struct DOSBase *        vc4_DOSBase;
    struct IntuitionBase *  vc4_IntuitionBase;
    APTR                    vc4_DeviceTreeBase;
    APTR                    vc4_MailBox;
    APTR                    vc4_HVS;
    APTR                    vc4_RequestBase;
    APTR                    vc4_Request;
    APTR                    vc4_MemBase;
    uint32_t                vc4_MemSize;
    APTR                    vc4_Framebuffer;
    uint32_t                vc4_Pitch;
    uint16_t                vc4_Enabled;
    uint8_t                 vc4_VideoCore6;

    struct Size             vc4_DispSize;

    int                     vc4_ActivePlane;
    int                     vc4_FreePlane;

    ULONG                   vc4_Scaler;
    UBYTE                   vc4_Phase;
    ULONG                   vc4_VertFreq;
    double                  vc4_Kernel_B;
    double                  vc4_Kernel_C;
    UBYTE                   vc4_UseKernel;
    UBYTE                   vc4_SpriteAlpha;
    UBYTE                   vc4_SpriteVisible;

    ULONG                   vc4_ScaleX;
    ULONG                   vc4_ScaleY;

    WORD                    vc4_MouseX;
    WORD                    vc4_MouseY;
    WORD                    vc4_OffsetX;
    WORD                    vc4_OffsetY;

    volatile ULONG *        vc4_PlaneCoord;
    volatile ULONG *        vc4_PlaneScalerX;
    volatile ULONG *        vc4_PlaneScalerY;
    volatile ULONG *        vc4_MouseCoord;
    volatile ULONG *        vc4_MousePalette;
    volatile ULONG *        vc4_PIPCoord;
    volatile ULONG *        vc4_Kernel;

    ULONG                   vc4_SpriteColors[3];

    struct MsgPort          *vc4_Port;
    struct Task             *vc4_Task;

    struct {
        APTR        lp_Addr;
        UWORD       lp_Width;
        WORD        lp_X;
        WORD        lp_Y;
        RGBFTYPE    lp_Format;
    }                       vc4_LastPanning;
    
    UBYTE *                 vc4_SpriteShape;

    ULONG                   vc4_VPUCopyBlock;
    ULONG                   vc4_VPUCopyRect;
    ULONG                   vc4_VPUCopyRectRev;
};

void bug(const char * restrict format, ...);

/* Endian support */

static inline uint64_t LE64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint32_t LE32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t LE16(uint16_t x) { return __builtin_bswap16(x); }

enum hvs_pixel_format {
    /* 8bpp */
    HVS_PIXEL_FORMAT_RGB332 = 0,
    /* 16bpp */
    HVS_PIXEL_FORMAT_RGBA4444 = 1,
    HVS_PIXEL_FORMAT_RGB555 = 2,
    HVS_PIXEL_FORMAT_RGBA5551 = 3,
    HVS_PIXEL_FORMAT_RGB565 = 4,
    /* 24bpp */
    HVS_PIXEL_FORMAT_RGB888 = 5,
    HVS_PIXEL_FORMAT_RGBA6666 = 6,
    /* 32bpp */
    HVS_PIXEL_FORMAT_RGBA8888 = 7,

    HVS_PIXEL_FORMAT_YCBCR_YUV420_3PLANE = 8,
    HVS_PIXEL_FORMAT_YCBCR_YUV420_2PLANE = 9,
    HVS_PIXEL_FORMAT_YCBCR_YUV422_3PLANE = 10,
    HVS_PIXEL_FORMAT_YCBCR_YUV422_2PLANE = 11,
    HVS_PIXEL_FORMAT_H264 = 12,
    HVS_PIXEL_FORMAT_PALETTE = 13,
    HVS_PIXEL_FORMAT_YUV444_RGB = 14,
    HVS_PIXEL_FORMAT_AYUV444_RGB = 15,
    HVS_PIXEL_FORMAT_RGBA1010102 = 16,
    HVS_PIXEL_FORMAT_YCBCR_10BIT = 17,
};

enum palette_type {
    PALETTE_NONE = 0, 
    PALETTE_1BPP = 1,
    PALETTE_2BPP = 2,
    PALETTE_4BPP = 3,
    PALETTE_8BPP = 4,
};

#define HVS_PIXEL_ORDER_RGBA                    0
#define HVS_PIXEL_ORDER_BGRA                    1
#define HVS_PIXEL_ORDER_ARGB                    2
#define HVS_PIXEL_ORDER_ABGR                    3

#define HVS_PIXEL_ORDER_XBRG                    0
#define HVS_PIXEL_ORDER_XRBG                    1
#define HVS_PIXEL_ORDER_XRGB                    2
#define HVS_PIXEL_ORDER_XBGR                    3

#define HVS_PIXEL_ORDER_XYCBCR			        0
#define HVS_PIXEL_ORDER_XYCRCB			        1
#define HVS_PIXEL_ORDER_YXCBCR			        2
#define HVS_PIXEL_ORDER_YXCRCB			        3

#define SCALER_CTL0_SCL_H_PPF_V_PPF             0
#define SCALER_CTL0_SCL_H_TPZ_V_PPF             1
#define SCALER_CTL0_SCL_H_PPF_V_TPZ             2
#define SCALER_CTL0_SCL_H_TPZ_V_TPZ             3
#define SCALER_CTL0_SCL_H_PPF_V_NONE            4
#define SCALER_CTL0_SCL_H_NONE_V_PPF            5
#define SCALER_CTL0_SCL_H_NONE_V_TPZ            6
#define SCALER_CTL0_SCL_H_TPZ_V_NONE            7

#define SCALER_DISPSTAT0                        0x00000048
#define SCALER_DISPSTAT1                        0x00000058
#define SCALER_DISPSTAT2                        0x00000068
#define SCALER_DISPSTATX_FRAME_COUNT_MASK       VC4_MASK(17, 12)
#define SCALER_DISPSTATX_FRAME_COUNT_SHIFT      12

#endif /* _EMU68_VC4_H */
