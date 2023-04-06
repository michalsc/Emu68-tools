#ifndef _VC4_H
#define _VC4_H

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

void SetDAC(struct BoardInfo *bi asm("a0"), RGBFTYPE format asm("d7"));
void SetGC(struct BoardInfo *bi asm("a0"), struct ModeInfo *mode_info asm("a1"), BOOL border asm("d0"));
UWORD SetSwitch (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));
void SetPanning (__REGA0(struct BoardInfo *b), __REGA1(UBYTE *addr), __REGD0(UWORD width), __REGD1(WORD x_offset), __REGD2(WORD y_offset), __REGD7(RGBFTYPE format));
void SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num));
UWORD CalculateBytesPerRow (__REGA0(struct BoardInfo *b), __REGD0(UWORD width), __REGD7(RGBFTYPE format));
APTR CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format));
ULONG GetCompatibleFormats (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
UWORD SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));
LONG ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format));
ULONG GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format));
void SetClock (__REGA0(struct BoardInfo *b));
void SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane));
void WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
BOOL GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL expected));
void FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format));
void InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format));
void BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format));
void BlitPlanar2Chunky (__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void BlitPlanar2Direct (__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGA3(struct ColorIndexMapping *clut), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void SetSprite (__REGA0(struct BoardInfo *b), __REGD0(BOOL enable), __REGD7(RGBFTYPE format));
void SetSpritePosition (__REGA0(struct BoardInfo *b), __REGD0(WORD x), __REGD1(WORD y), __REGD7(RGBFTYPE format));
void SetSpriteImage (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void SetSpriteColor (__REGA0(struct BoardInfo *b), __REGD0(UBYTE idx), __REGD1(UBYTE R), __REGD2(UBYTE G), __REGD3(UBYTE B), __REGD7(RGBFTYPE format));
ULONG GetVBeamPos(struct BoardInfo *b asm("a0"));

extern int unity_kernel;
extern int kernel_start;

int compute_nearest_neighbour_kernel(uint32_t *dlist_memory);
int compute_scaling_kernel(uint32_t *dlist_memory, double b, double c);

#endif /* _VC4_H */
