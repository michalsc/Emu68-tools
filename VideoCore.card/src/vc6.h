#ifndef _VC6_H
#define _VC6_H

#include <common/compiler.h>
#include <stdint.h>
#include "boardinfo.h"

#define VC6_CONTROL_FORMAT(n)       (n & 0x1f)
#define VC6_CONTROL_END             (1<<31)
#define VC6_CONTROL_VALID           (1<<30)
#define VC6_CONTROL_WORDS(n)        (((n) & 0x3f) << 24)
#define VC6_CONTROL0_FIXED_ALPHA    (1<<19)
#define VC6_CONTROL0_HFLIP          (1<<31)
#define VC6_CONTROL0_VFLIP          (1<<15)
#define VC6_CONTROL_PIXEL_ORDER(n)  ((n & 3) << 13)
#define VC6_CONTROL_SCL1(scl)       ((scl) << 8)
#define VC6_CONTROL_SCL0(scl)       ((scl) << 5)
#define VC6_CONTROL_UNITY           (1<<15)
#define VC6_CONTROL_ALPHA_EXPAND    (1<<12)
#define VC6_CONTROL_RGB_EXPAND      (1<<11)

#define VC6_POS0_X(n) (n & 0x2fff)
#define VC6_POS0_Y(n) ((n & 0x2fff) << 16)

#define VC6_POS1_W(n) (n & 0xffff)
#define VC6_POS1_H(n) ((n & 0xffff) << 16)

#define VC6_POS2_W(n) (n & 0xffff)
#define VC6_POS2_H(n) ((n & 0xffff) << 16)

#define VC6_SCALER_POS2_ALPHA_MODE_MASK             0xc0000000
#define VC6_SCALER_POS2_ALPHA_MODE_SHIFT            30
#define VC6_SCALER_POS2_ALPHA_MODE_PIPELINE         0
#define VC6_SCALER_POS2_ALPHA_MODE_FIXED            1
#define VC6_SCALER_POS2_ALPHA_MODE_FIXED_NONZERO    2
#define VC6_SCALER_POS2_ALPHA_MODE_FIXED_OVER_0x07  3
#define VC6_SCALER_POS2_ALPHA_PREMULT               (1 << 29)
#define VC6_SCALER_POS2_ALPHA_MIX                   (1 << 28)
#define VC6_SCALER_POS2_ALPHA(n)                    (((n) << 4) & 0xfff0)

#define VC6_SCALER_POS2_HEIGHT_MASK                 0x3fff0000
#define VC6_SCALER_POS2_HEIGHT_SHIFT                16

#define VC6_SCALER_POS2_WIDTH_MASK                  0x00003fff
#define VC6_SCALER_POS2_WIDTH_SHIFT                 0

void    VC6_SetDAC(REGARG(struct BoardInfo *bi, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetGC(REGARG(struct BoardInfo *bi, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(BOOL border, "d0"));
UWORD   VC6_SetSwitch(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD enabled, "d0"));
void    VC6_SetPanning(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE *addr, "a1"), REGARG(UWORD width, "d0"), REGARG(WORD x_offset, "d1"), REGARG(WORD y_offset, "d2"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetColorArray(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD start, "d0"), REGARG(UWORD num, "d1"));
UWORD   VC6_CalculateBytesPerRow(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD width, "d0"), REGARG(RGBFTYPE format, "d7"));
APTR    VC6_CalculateMemory(REGARG(struct BoardInfo *b, "a0"), REGARG(unsigned long addr, "a1"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC6_GetCompatibleFormats(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
UWORD   VC6_SetDisplay(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD enabled, "d0"));
LONG    VC6_ResolvePixelClock(REGARG(struct BoardInfo *b, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(ULONG pixel_clock, "d0"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC6_GetPixelClock(REGARG(struct BoardInfo *b, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(ULONG index, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetClock(REGARG(struct BoardInfo *b, "a0"));
void    VC6_SetMemoryMode(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetWriteMask(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE mask, "d0"));
void    VC6_SetClearMask(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE mask, "d0"));
void    VC6_SetReadPlane(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE plane, "d0"));
void    VC6_WaitVerticalSync(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL toggle, "d0"));
BOOL    VC6_GetVSyncState(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL toggle, "d0"));
void    VC6_FillRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(ULONG color, "d4"), REGARG(UBYTE mask, "d5"), REGARG(RGBFTYPE format, "d7"));
void    VC6_InvertRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC6_BlitRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD dx, "d2"), REGARG(WORD dy, "d3"), REGARG(WORD w, "d4"), REGARG(WORD h, "d5"), REGARG(UBYTE mask, "d6"), REGARG(RGBFTYPE format, "d7"));
void    VC6_BlitRectNoMaskComplete(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *rs, "a1"), REGARG(struct RenderInfo *rt, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD dx, "d2"), REGARG(WORD dy, "d3"), REGARG(WORD w, "d4"), REGARG(WORD h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(RGBFTYPE format, "d7"));
void    VC6_BlitTemplate(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Template *t, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC6_BlitPattern(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Pattern *p, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC6_DrawLine(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Line *l, "a2"), REGARG(UBYTE mask, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_BlitPlanar2Chunky(REGARG(struct BoardInfo *b, "a0"), REGARG(struct BitMap *bm, "a1"), REGARG(struct RenderInfo *r, "a2"), REGARG(SHORT x, "d0"), REGARG(SHORT y, "d1"), REGARG(SHORT dx, "d2"), REGARG(SHORT dy, "d3"), REGARG(SHORT w, "d4"), REGARG(SHORT h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(UBYTE mask, "d7"));
void    VC6_BlitPlanar2Direct(REGARG(struct BoardInfo *b, "a0"), REGARG(struct BitMap *bm, "a1"), REGARG(struct RenderInfo *r, "a2"), REGARG(struct ColorIndexMapping *clut, "a3"), REGARG(SHORT x, "d0"), REGARG(SHORT y, "d1"), REGARG(SHORT dx, "d2"), REGARG(SHORT dy, "d3"), REGARG(SHORT w, "d4"), REGARG(SHORT h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(UBYTE mask, "d7"));
void    VC6_SetSprite(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL enable, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetSpritePosition(REGARG(struct BoardInfo *b, "a0"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetSpriteImage(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC6_SetSpriteColor(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE idx, "d0"), REGARG(UBYTE R, "d1"), REGARG(UBYTE G, "d2"), REGARG(UBYTE B, "d3"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC6_GetVBeamPos(REGARG(struct BoardInfo *b, "a0"));
void *  VC6_CreateFeature(REGARG(struct BoardInfo *b, "a0"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a1"));
BOOL    VC6_DeleteFeature(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR feature, "a1"), REGARG(ULONG type, "d0"));
ULONG   VC6_GetFeatureAttrs(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR featuredata, "a1"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a2"));
ULONG   VC6_SetFeatureAttrs(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR featuredata, "a1"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a2"));

struct VC6MemWindow {
    struct BitMap *     mw_BitMap;
    struct ModeInfo *   mw_ModeInfo;
    APTR                mw_Memory;
    UWORD               mw_SourceWidth;
    UWORD               mw_SourceHeight;
    ULONG               mw_Format;
    ULONG               mw_ModeFormat;
    ULONG *             mw_DisplayList;
    ULONG               mw_DisplayListSize;
    UWORD               mw_ClipLeft;
    UWORD               mw_ClipTop;
    UWORD               mw_ClipWidth;
    UWORD               mw_ClipHeight;
};

extern int unity_kernel;
extern int kernel_start;

int compute_nearest_neighbour_kernel(volatile uint32_t *dlist_memory, ULONG offset);
int compute_scaling_kernel(volatile uint32_t *dlist_memory, ULONG offset, ULONG b, ULONG c);

void VC6_ConstructUnicamDL(struct VC4Base *VC4Base);

#endif /* _VC6_H */
