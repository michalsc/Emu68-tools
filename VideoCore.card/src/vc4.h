#ifndef _VC4_H
#define _VC4_H

#include <common/compiler.h>
#include <stdint.h>
#include "boardinfo.h"

#define CONTROL_FORMAT(n)       (n & 0xf)
#define CONTROL_END             (1<<31)
#define CONTROL_VALID           (1<<30)
#define CONTROL_WORDS(n)        (((n) & 0x3f) << 24)
#define CONTROL0_FIXED_ALPHA    (1<<19)
#define CONTROL0_HFLIP          (1<<16)
#define CONTROL0_VFLIP          (1<<15)
#define CONTROL_PIXEL_ORDER(n)  ((n & 3) << 13)
#define CONTROL_SCL1(scl)       ((scl) << 8)
#define CONTROL_SCL0(scl)       ((scl) << 5)
#define CONTROL_UNITY           (1<<4)

#define POS0_X(n) (n & 0xfff)
#define POS0_Y(n) ((n & 0xfff) << 12)
#define POS0_ALPHA(n) ((n & 0xff) << 24)

#define POS1_W(n) (n & 0xffff)
#define POS1_H(n) ((n & 0xffff) << 16)

#define POS2_W(n) (n & 0xffff)
#define POS2_H(n) ((n & 0xffff) << 16)

#define SCALER_POS2_ALPHA_MODE_MASK             0xc0000000
#define SCALER_POS2_ALPHA_MODE_SHIFT            30
#define SCALER_POS2_ALPHA_MODE_PIPELINE         0
#define SCALER_POS2_ALPHA_MODE_FIXED            1
#define SCALER_POS2_ALPHA_MODE_FIXED_NONZERO    2
#define SCALER_POS2_ALPHA_MODE_FIXED_OVER_0x07  3
#define SCALER_POS2_ALPHA_PREMULT               (1 << 29)
#define SCALER_POS2_ALPHA_MIX                   (1 << 28)

#define SCALER_POS2_HEIGHT_MASK                 0x0fff0000
#define SCALER_POS2_HEIGHT_SHIFT                16

#define SCALER_POS2_WIDTH_MASK                  0x00000fff
#define SCALER_POS2_WIDTH_SHIFT                 0

void    VC4_SetDAC(REGARG(struct BoardInfo *bi, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetGC(REGARG(struct BoardInfo *bi, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(BOOL border, "d0"));
UWORD   VC4_SetSwitch(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD enabled, "d0"));
void    VC4_SetPanning(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE *addr, "a1"), REGARG(UWORD width, "d0"), REGARG(WORD x_offset, "d1"), REGARG(WORD y_offset, "d2"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetColorArray(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD start, "d0"), REGARG(UWORD num, "d1"));
UWORD   VC4_CalculateBytesPerRow(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD width, "d0"), REGARG(RGBFTYPE format, "d7"));
APTR    VC4_CalculateMemory(REGARG(struct BoardInfo *b, "a0"), REGARG(unsigned long addr, "a1"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC4_GetCompatibleFormats(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
UWORD   VC4_SetDisplay(REGARG(struct BoardInfo *b, "a0"), REGARG(UWORD enabled, "d0"));
LONG    VC4_ResolvePixelClock(REGARG(struct BoardInfo *b, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(ULONG pixel_clock, "d0"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC4_GetPixelClock(REGARG(struct BoardInfo *b, "a0"), REGARG(struct ModeInfo *mode_info, "a1"), REGARG(ULONG index, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetClock(REGARG(struct BoardInfo *b, "a0"));
void    VC4_SetMemoryMode(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetWriteMask(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE mask, "d0"));
void    VC4_SetClearMask(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE mask, "d0"));
void    VC4_SetReadPlane(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE plane, "d0"));
void    VC4_WaitVerticalSync(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL toggle, "d0"));
BOOL    VC4_GetVSyncState(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL toggle, "d0"));
void    VC4_FillRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(ULONG color, "d4"), REGARG(UBYTE mask, "d5"), REGARG(RGBFTYPE format, "d7"));
void    VC4_InvertRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC4_BlitRect(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD dx, "d2"), REGARG(WORD dy, "d3"), REGARG(WORD w, "d4"), REGARG(WORD h, "d5"), REGARG(UBYTE mask, "d6"), REGARG(RGBFTYPE format, "d7"));
void    VC4_BlitRectNoMaskComplete(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *rs, "a1"), REGARG(struct RenderInfo *rt, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD dx, "d2"), REGARG(WORD dy, "d3"), REGARG(WORD w, "d4"), REGARG(WORD h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(RGBFTYPE format, "d7"));
void    VC4_BlitTemplate(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Template *t, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC4_BlitPattern(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Pattern *p, "a2"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(WORD w, "d2"), REGARG(WORD h, "d3"), REGARG(UBYTE mask, "d4"), REGARG(RGBFTYPE format, "d7"));
void    VC4_DrawLine(REGARG(struct BoardInfo *b, "a0"), REGARG(struct RenderInfo *r, "a1"), REGARG(struct Line *l, "a2"), REGARG(UBYTE mask, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_BlitPlanar2Chunky(REGARG(struct BoardInfo *b, "a0"), REGARG(struct BitMap *bm, "a1"), REGARG(struct RenderInfo *r, "a2"), REGARG(SHORT x, "d0"), REGARG(SHORT y, "d1"), REGARG(SHORT dx, "d2"), REGARG(SHORT dy, "d3"), REGARG(SHORT w, "d4"), REGARG(SHORT h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(UBYTE mask, "d7"));
void    VC4_BlitPlanar2Direct(REGARG(struct BoardInfo *b, "a0"), REGARG(struct BitMap *bm, "a1"), REGARG(struct RenderInfo *r, "a2"), REGARG(struct ColorIndexMapping *clut, "a3"), REGARG(SHORT x, "d0"), REGARG(SHORT y, "d1"), REGARG(SHORT dx, "d2"), REGARG(SHORT dy, "d3"), REGARG(SHORT w, "d4"), REGARG(SHORT h, "d5"), REGARG(UBYTE minterm, "d6"), REGARG(UBYTE mask, "d7"));
void    VC4_SetSprite(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL enable, "d0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetSpritePosition(REGARG(struct BoardInfo *b, "a0"), REGARG(WORD x, "d0"), REGARG(WORD y, "d1"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetSpriteImage(REGARG(struct BoardInfo *b, "a0"), REGARG(RGBFTYPE format, "d7"));
void    VC4_SetSpriteColor(REGARG(struct BoardInfo *b, "a0"), REGARG(UBYTE idx, "d0"), REGARG(UBYTE R, "d1"), REGARG(UBYTE G, "d2"), REGARG(UBYTE B, "d3"), REGARG(RGBFTYPE format, "d7"));
ULONG   VC4_GetVBeamPos(REGARG(struct BoardInfo *b, "a0"));
void *  VC4_CreateFeature(REGARG(struct BoardInfo *b, "a0"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a1"));
BOOL    VC4_DeleteFeature(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR feature, "a1"), REGARG(ULONG type, "d0"));
ULONG   VC4_GetFeatureAttrs(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR featuredata, "a1"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a2"));
ULONG   VC4_SetFeatureAttrs(REGARG(struct BoardInfo *b, "a0"), REGARG(APTR featuredata, "a1"), REGARG(ULONG type, "d0"), REGARG(struct TagItem *tags, "a2"));

extern int unity_kernel;
extern int kernel_start;

int compute_nearest_neighbour_kernel(volatile uint32_t *dlist_memory, ULONG offset);
int compute_scaling_kernel(volatile uint32_t *dlist_memory, ULONG offset, ULONG b, ULONG c);

void VC4_ConstructUnicamDL(struct VC4Base *VC4Base);

#endif /* _VC4_H */
