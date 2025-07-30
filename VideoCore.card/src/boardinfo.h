#ifndef boardinfo_H
#define boardinfo_H

#ifndef  LIBRARIES_PICASSO96_H
#include        <libraries/Picasso96.h>
#endif

#ifndef  EXEC_INTERRUPTS_H
#include        <exec/interrupts.h>
#endif

#ifndef  EXEC_LIBRARIES_H
#include <exec/libraries.h>
#endif

#ifndef  EXEC_SEMAPHORES_H
#include        <exec/semaphores.h>
#endif

#ifndef  GRAPHICS_GFX_H
#include        <graphics/gfx.h>
#endif

#ifndef  GRAPHICS_VIEW_H
#include        <graphics/view.h>
#endif

#ifndef DEVICES_TIMER_H
#include <devices/timer.h>
#endif

#ifndef settings_H
#include        "settings.h"
#endif

#include <common/compiler.h>

#define ASM

/************************************************************************/

#define MAXSPRITEWIDTH 32
#define MAXSPRITEHEIGHT 48

/************************************************************************/

#define DI_P96_INVALID  0x1000
#define DI_P96_MONITOOL 0x2000

/************************************************************************/
/* Types for BoardType Identification
 */
typedef enum {
        BT_NoBoard,
        BT_oMniBus,
        BT_Graffity,
        BT_CyberVision,
        BT_Domino,
        BT_Merlin,
        BT_PicassoII,
        BT_Piccolo,
        BT_RetinaBLT,
        BT_Spectrum,
        BT_PicassoIV,
        BT_PiccoloSD64,
        BT_A2410,
        BT_Pixel64,
        BT_uaegfx,              // 14
        BT_CVision3D,
        BT_Altais,
        BT_Prometheus,
        BT_Mediator,
        BT_powerfb,
        BT_powerpci,
        BT_CVisionPPC,
        BT_GREX,
        BT_Prototype7,
        BT_Reserved,
        BT_Reserved2,
	BT_MNT_VA2000,
	BT_MNT_ZZ9000,
        BT_MaxBoardTypes
} BTYPE;

/************************************************************************/
/* Types for PaletteChipType Identification
 */
typedef enum {
        PCT_Unknown,
        PCT_S11483,             // Sierra S11483: HiColor 15 bit, oMniBus, Domino
        PCT_S15025,             // Sierra S15025: TrueColor 32 bit, oMniBus
        PCT_CirrusGD542x,       // Cirrus GD542x internal: TrueColor 24 bit
        PCT_Domino,             // is in fact a Sierra S11483
        PCT_BT482,              // BrookTree BT482: TrueColor 32 bit, Merlin
        PCT_Music,              // Music MU9C4910: TrueColor 24 bit, oMniBus
        PCT_ICS5300,            // ICS 5300: ...., Retina BLT Z3
        PCT_CirrusGD5446,       // Cirrus GD5446 internal: TrueColor 24 bit
        PCT_CirrusGD5434,       // Cirrus GD5434 internal: TrueColor 32 bit
        PCT_S3Trio64,           // S3 Trio64 internal: TrueColor 32 bit
        PCT_A2410_xxx,          // A2410 DAC, *type unknown*
        PCT_S3ViRGE,            // S3 ViRGE internal: TrueColor 32 bit
        PCT_3dfxVoodoo,         // 3dfx Voodoo internal
        PCT_TIPermedia2,        // TexasInstruments TVP4020 Permedia2 internal
        PCT_ATIRV100,           // ATI Technologies Radeon/Radeon 7000 internal
	PCT_reserved,
	PCT_reserved2,
	PCT_MNT_VA2000,
	PCT_MNT_ZZ9000,
        PCT_MaxPaletteChipTypes
} PCTYPE;

/************************************************************************/
/* Types for GraphicsControllerType Identification
 */
typedef enum {
        GCT_Unknown,
        GCT_ET4000,
        GCT_ETW32,      
        GCT_CirrusGD542x,
        GCT_NCR77C32BLT,        
        GCT_CirrusGD5446,
        GCT_CirrusGD5434,
        GCT_S3Trio64,
        GCT_TI34010,
        GCT_S3ViRGE,
        GCT_3dfxVoodoo,
        GCT_TIPermedia2,
        GCT_ATIRV100,
        GCT_reserved,
        GCT_reserved2,
	GCT_MNT_VA2000,
	GCT_MNT_ZZ9000,
        GCT_MaxGraphicsControllerTypes
} GCTYPE;

/************************************************************************/

#define RGBFF_PLANAR    RGBFF_NONE
#define RGBFF_CHUNKY    RGBFF_CLUT

#define RGBFB_PLANAR    RGBFB_NONE
#define RGBFB_CHUNKY    RGBFB_CLUT

/************************************************************************/

enum{
        DPMS_ON,                /* Full operation                             */
        DPMS_STANDBY,           /* Optional state of minimal power reduction  */
        DPMS_SUSPEND,           /* Significant reduction of power consumption */
        DPMS_OFF                /* Lowest level of power consumption          */
};

/************************************************************************/

struct CLUTEntry {
        UBYTE   Red;
        UBYTE   Green;
        UBYTE   Blue;
};

struct ColorIndexMapping {
        ULONG   ColorMask;
        ULONG   Colors[256];
};

/************************************************************************/

struct GfxMemChunk {
        struct MinNode Node;
        char *Ptr;
        ULONG Size;
        BOOL Used;
};

/************************************************************************/

struct Template {
        APTR                    Memory;
        WORD                    BytesPerRow;
        UBYTE                   XOffset;                                // 0 <= XOffset <= 15
        UBYTE                   DrawMode;
        ULONG                   FgPen;
        ULONG                   BgPen;
};

/************************************************************************/

struct Pattern {
        APTR                    Memory;
        UWORD                   XOffset, YOffset;
        ULONG                   FgPen, BgPen;
        UBYTE                   Size;                                   // Width: 16, Height: (1<<pat_Size)
        UBYTE                   DrawMode;
};

/************************************************************************/

struct Line {
        WORD                    X, Y;
        UWORD                   Length;
        WORD                    dX, dY;
        WORD                    sDelta, lDelta, twoSDminusLD;
        UWORD                   LinePtrn;
        UWORD                   PatternShift;
        ULONG                   FgPen, BgPen;
        BOOL                    Horizontal;
        UBYTE                   DrawMode;
        BYTE                    pad;
        UWORD                   Xorigin, Yorigin;
};

/************************************************************************/

struct BitMapExtra {
        struct MinNode          BoardNode;
        struct BitMapExtra *    HashChain;
        APTR                    Match;
        struct BitMap *         BitMap;
        struct BoardInfo *      BoardInfo;
        APTR                    MemChunk;
        struct RenderInfo       RenderInfo;
        UWORD                   Width, Height;
        UWORD                   Flags;
        // NEW !!!
        WORD                    BaseLevel, CurrentLevel;
        struct BitMapExtra *    CompanionMaster;
};

/* BitMapExtra flags */
#define BMEF_ONBOARD            0x0001
#define BMEF_SPECIAL            0x0002
#define  BMEF_VISIBLE           0x0800
#define  BMEF_DISPLAYABLE       0x1000
#define BMEF_SPRITESAVED        0x2000
#define BMEF_CHECKSPRITE        0x4000
#define BMEF_INUSE              0x8000

/************************************************************************/

struct SpecialFeature {
        struct MinNode          Node;
        struct BoardInfo *      BoardInfo;
        struct BitMap *         BitMap;
        ULONG                   Type;
        APTR                    FeatureData;
};

enum  {
        SFT_INVALID, SFT_FLICKERFIXER, SFT_VIDEOCAPTURE, SFT_VIDEOWINDOW, SFT_MEMORYWINDOW
};

#define FA_Restore              (TAG_USER+0)                    /* becomes visible again */
#define FA_Onboard              (TAG_USER+1)
#define FA_Active               (TAG_USER+2)
#define FA_Left                 (TAG_USER+3)
#define FA_Top                  (TAG_USER+4)
#define FA_Width                (TAG_USER+5)
#define FA_Height               (TAG_USER+6)
#define FA_Format               (TAG_USER+7)
#define FA_Color                (TAG_USER+8)
#define FA_Occlusion            (TAG_USER+9)
#define FA_SourceWidth          (TAG_USER+10)
#define FA_SourceHeight         (TAG_USER+11)
#define FA_MinWidth             (TAG_USER+12)
#define FA_MinHeight            (TAG_USER+13)
#define FA_MaxWidth             (TAG_USER+14)
#define FA_MaxHeight            (TAG_USER+15)
#define FA_Interlace            (TAG_USER+16)
#define FA_PAL                  (TAG_USER+17)
#define FA_BitMap               (TAG_USER+18)
#define FA_Brightness           (TAG_USER+19)
#define FA_ModeInfo             (TAG_USER+20)
#define FA_ModeFormat           (TAG_USER+21)
#define FA_Colors               (TAG_USER+22)
#define FA_Colors32             (TAG_USER+23)
#define FA_NoMemory             (TAG_USER+24)
#define FA_RenderFunc           (TAG_USER+25)
#define FA_SaveFunc             (TAG_USER+26)
#define FA_UserData             (TAG_USER+27)
#define FA_Alignment            (TAG_USER+28)
#define FA_ConstantBytesPerRow  (TAG_USER+29)
#define FA_DoubleBuffer         (TAG_USER+30)
#define FA_Pen                  (TAG_USER+31)
#define FA_ModeMemorySize       (TAG_USER+32)
#define FA_ClipLeft             (TAG_USER+33)
#define FA_ClipTop              (TAG_USER+34)
#define FA_ClipWidth            (TAG_USER+35)
#define FA_ClipHeight           (TAG_USER+36)
#define FA_ConstantByteSwapping (TAG_USER+37)

/************************************************************************/

/* Tags for bi->AllocBitMap() */

#define ABMA_Friend                  (TAG_USER+0)
#define ABMA_Depth                   (TAG_USER+1)
#define ABMA_RGBFormat               (TAG_USER+2)
#define ABMA_Clear                   (TAG_USER+3)
#define ABMA_Displayable             (TAG_USER+4)
#define ABMA_Visible                 (TAG_USER+5)
#define ABMA_NoMemory                (TAG_USER+6)
#define ABMA_NoSprite                (TAG_USER+7)
#define ABMA_Colors                  (TAG_USER+8)
#define ABMA_Colors32                (TAG_USER+9)
#define ABMA_ModeWidth               (TAG_USER+10)
#define ABMA_ModeHeight              (TAG_USER+11)
#define ABMA_RenderFunc              (TAG_USER+12)
#define ABMA_SaveFunc                (TAG_USER+13)
#define ABMA_UserData                (TAG_USER+14)
#define ABMA_Alignment               (TAG_USER+15)
#define ABMA_ConstantBytesPerRow     (TAG_USER+16)
#define ABMA_UserPrivate             (TAG_USER+17)
#define ABMA_ConstantByteSwapping    (TAG_USER+18)
/*
 * THOR: New for V45 Gfx/Intuiton
 * "by accident", this is identically to SA_DisplayID of intuition
 * resp. SA_Behind, SA_Colors, SA_Colors32
 */
#define ABMA_DisplayID               (TAG_USER + 32 + 0x12)
#define ABMA_BitmapInvisible         (TAG_USER + 32 + 0x17)
#define ABMA_BitmapColors            (TAG_USER + 32 + 0x09)
#define ABMA_BitmapColors32          (TAG_USER + 32 + 0x23)

/************************************************************************/

/* Tags for bi->GetBitMapAttr() */

#define GBMA_MEMORY                  (TAG_USER+0)
#define GBMA_BASEMEMORY              (TAG_USER+1)
#define GBMA_BYTESPERROW             (TAG_USER+2)
#define GBMA_BYTESPERPIXEL           (TAG_USER+3)
#define GBMA_BITSPERPIXEL            (TAG_USER+4)
#define GBMA_RGBFORMAT               (TAG_USER+6)
#define GBMA_WIDTH                   (TAG_USER+7)
#define GBMA_HEIGHT                  (TAG_USER+8)
#define GBMA_DEPTH                   (TAG_USER+9)

/************************************************************************/

struct BoardInfo{
        UBYTE *                 RegisterBase;
        UBYTE *                 MemoryBase;
        UBYTE *                 MemoryIOBase;
        ULONG                   MemorySize;
        char *                  BoardName,VBIName[32];
        struct CardBase *       CardBase;
        struct ChipBase *       ChipBase;
        struct ExecBase *       ExecBase;
        struct Library *        UtilBase;
        struct Interrupt        HardInterrupt;
        struct Interrupt        SoftInterrupt;
        struct SignalSemaphore  BoardLock;
        struct MinList          ResolutionsList;
        BTYPE                   BoardType;
        PCTYPE                  PaletteChipType;
        GCTYPE                  GraphicsControllerType;
        UWORD                   MoniSwitch;
        UWORD                   BitsPerCannon;
        ULONG                   Flags;
        UWORD                   SoftSpriteFlags;
        UWORD                   ChipFlags;                                      // private, chip specific, not touched by RTG
        ULONG                   CardFlags;                                      // private, card specific, not touched by RTG

        UWORD                   BoardNum;                                       // set by rtg.library
        UWORD                   RGBFormats;

        UWORD                   MaxHorValue[MAXMODES];
        UWORD                   MaxVerValue[MAXMODES];
        UWORD                   MaxHorResolution[MAXMODES];
        UWORD                   MaxVerResolution[MAXMODES];
        ULONG                   MaxMemorySize, MaxChunkSize;

        ULONG                   MemoryClock;

        ULONG                   PixelClockCount[MAXMODES];

        APTR ASM              (*AllocCardMem)(REGARG(struct BoardInfo *bi, "a0"), REGARG(ULONG size, "d0"), REGARG(BOOL force, "d1"), REGARG(BOOL system, "d2"));
        BOOL ASM              (*FreeCardMem)(REGARG(struct BoardInfo *bi, "a0"), REGARG(APTR membase, "a1"));

        BOOL ASM              (*SetSwitch)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"));

        void ASM              (*SetColorArray)(REGARG(struct BoardInfo *, "a0"), REGARG(UWORD, "d0"), REGARG(UWORD, "d1"));

        void ASM              (*SetDAC)(REGARG(struct BoardInfo *, "a0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetGC)(REGARG(struct BoardInfo *, "a0"), REGARG(struct ModeInfo *, "a1"), REGARG(BOOL, "d0"));
        void ASM              (*SetPanning)(REGARG(struct BoardInfo *, "a0"), REGARG(UBYTE *, "a1"), REGARG(UWORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(RGBFTYPE, "d7"));
        UWORD ASM             (*CalculateBytesPerRow)(REGARG(struct BoardInfo *, "a0"), REGARG(UWORD, "d0"), REGARG(RGBFTYPE, "d7"));
        APTR ASM              (*CalculateMemory)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(RGBFTYPE, "d7"));
        ULONG ASM             (*GetCompatibleFormats)(REGARG(struct BoardInfo *, "a0"), REGARG(RGBFTYPE, "d7"));
        BOOL ASM              (*SetDisplay)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"));

        LONG ASM              (*ResolvePixelClock)(REGARG(struct BoardInfo *, "a0"), REGARG(struct ModeInfo *, "a1"), REGARG(ULONG, "d0"), REGARG(RGBFTYPE, "d7"));
        ULONG ASM             (*GetPixelClock)(REGARG(struct BoardInfo *bi, "a0"), REGARG(struct ModeInfo *, "a1"), REGARG(ULONG, "d0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetClock)(REGARG(struct BoardInfo *, "a0"));

        void ASM              (*SetMemoryMode)(REGARG(struct BoardInfo *, "a0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetWriteMask)(REGARG(struct BoardInfo *, "a0"), REGARG(UBYTE, "d0"));
        void ASM              (*SetClearMask)(REGARG(struct BoardInfo *, "a0"), REGARG(UBYTE, "d0"));
        void ASM              (*SetReadPlane)(REGARG(struct BoardInfo *, "a0"), REGARG(UBYTE, "d0"));

        void ASM              (*WaitVerticalSync)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"));
        BOOL ASM              (*SetInterrupt)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"));

        void ASM              (*WaitBlitter)(REGARG(struct BoardInfo *, "a0"));

        void ASM              (*ScrollPlanar)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(UWORD, "d0"), REGARG(UWORD, "d1"), REGARG(UWORD, "d2"), REGARG(UWORD, "d3"), REGARG(UWORD, "d4"), REGARG(UWORD, "d5"), REGARG(UBYTE, "d6"));
        void ASM              (*ScrollPlanarDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(UWORD, "d0"), REGARG(UWORD, "d1"), REGARG(UWORD, "d2"), REGARG(UWORD, "d3"), REGARG(UWORD, "d4"), REGARG(UWORD, "d5"), REGARG(UBYTE, "d6"));
        void ASM              (*UpdatePlanar)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(UBYTE, "d4"));
        void ASM              (*UpdatePlanarDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(UBYTE, "d4"));
        void ASM              (*BlitPlanar2Chunky)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(UBYTE, "d6"), REGARG(UBYTE, "d7"));
        void ASM              (*BlitPlanar2ChunkyDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(UBYTE, "d6"), REGARG(UBYTE, "d7"));

        void ASM              (*FillRect)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(ULONG, "d4"), REGARG(UBYTE, "d5"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*FillRectDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(ULONG, "d4"), REGARG(UBYTE, "d5"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*InvertRect)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*InvertRectDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitRect)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(WORD, "d4"), REGARG(WORD, "d5"), REGARG(UBYTE, "d6"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitRectDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(WORD, "d4"), REGARG(WORD, "d5"), REGARG(UBYTE, "d6"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitTemplate)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Template *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitTemplateDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Template *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitPattern)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Pattern *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitPatternDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Pattern *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(UBYTE, "d4"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*DrawLine)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Line *, "a2"), REGARG(UBYTE, "d0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*DrawLineDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct Line *, "a2"), REGARG(UBYTE, "d0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitRectNoMaskComplete)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(WORD, "d4"), REGARG(WORD, "d5"), REGARG(UBYTE, "d6"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitRectNoMaskCompleteDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct RenderInfo*, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(WORD, "d2"), REGARG(WORD, "d3"), REGARG(WORD, "d4"), REGARG(WORD, "d5"), REGARG(UBYTE, "d6"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*BlitPlanar2Direct)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(struct ColorIndexMapping *, "a3"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(UBYTE, "d6"), REGARG(UBYTE, "d7"));
        void ASM              (*BlitPlanar2DirectDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct RenderInfo *, "a2"), REGARG(struct ColorIndexMapping *, "a3"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(UBYTE, "d6"), REGARG(UBYTE, "d7"));
        BOOL ASM              (*EnableSoftSprite)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG formatflags, "d0"),REGARG(struct ModeInfo *, "a1"));
        BOOL ASM              (*EnableSoftSpriteDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG formatflags, "d0"),REGARG(struct ModeInfo *, "a1"));
        APTR ASM              (*AllocCardMemAbs)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG size, "d0"), REGARG(char *target, "a1"));
        void ASM              (*SetSplitPosition)(REGARG(struct BoardInfo *, "a0"), REGARG(SHORT, "d0"));
        void ASM              (*ReInitMemory)(REGARG(struct BoardInfo *, "a0"), REGARG(RGBFTYPE, "d0"));
        void ASM              (*Reserved2Default)(REGARG(struct BoardInfo *, "a0"));
        void ASM              (*Reserved3)(REGARG(struct BoardInfo *, "a0"));
        void ASM              (*Reserved3Default)(REGARG(struct BoardInfo *, "a0"));

        int ASM               (*WriteYUVRect)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(struct TagItem *, "a3"));
        int ASM               (*WriteYUVRectDefault)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(SHORT, "d0"), REGARG(SHORT, "d1"), REGARG(struct RenderInfo *, "a2"), REGARG(SHORT, "d2"), REGARG(SHORT, "d3"), REGARG(SHORT, "d4"), REGARG(SHORT, "d5"), REGARG(struct TagItem *, "a3"));

        BOOL ASM              (*GetVSyncState)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"));
        ULONG ASM             (*GetVBeamPos)(REGARG(struct BoardInfo *, "a0"));
        void ASM              (*SetDPMSLevel)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG, "d0"));
        void ASM              (*ResetChip)(REGARG(struct BoardInfo *, "a0"));
        ULONG ASM             (*GetFeatureAttrs)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(ULONG, "d0"), REGARG(struct TagItem *, "a2"));

        struct BitMap * ASM   (*AllocBitMap)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG, "d0"), REGARG(ULONG, "d1"), REGARG(struct TagItem *, "a1"));
        BOOL ASM              (*FreeBitMap)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(struct TagItem *, "a2"));
        ULONG ASM             (*GetBitMapAttr)(REGARG(struct BoardInfo *, "a0"), REGARG(struct BitMap *, "a1"), REGARG(ULONG, "d0"));

        BOOL ASM              (*SetSprite)(REGARG(struct BoardInfo *, "a0"), REGARG(BOOL, "d0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetSpritePosition)(REGARG(struct BoardInfo *, "a0"), REGARG(WORD, "d0"), REGARG(WORD, "d1"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetSpriteImage)(REGARG(struct BoardInfo *, "a0"), REGARG(RGBFTYPE, "d7"));
        void ASM              (*SetSpriteColor)(REGARG(struct BoardInfo *, "a0"), REGARG(UBYTE, "d0"), REGARG(UBYTE, "d1"), REGARG(UBYTE, "d2"), REGARG(UBYTE, "d3"), REGARG(RGBFTYPE, "d7"));

        APTR ASM              (*CreateFeature)(REGARG(struct BoardInfo *, "a0"), REGARG(ULONG, "d0"), REGARG(struct TagItem *, "a1"));
        ULONG ASM             (*SetFeatureAttrs)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(ULONG, "d0"), REGARG(struct TagItem *, "a2"));
        BOOL ASM              (*DeleteFeature)(REGARG(struct BoardInfo *, "a0"), REGARG(APTR, "a1"), REGARG(ULONG, "d0"));
        struct MinList          SpecialFeatures;

        struct ModeInfo *       ModeInfo;                                                              /* Chip Settings Stuff */
        RGBFTYPE                RGBFormat;
        WORD                    XOffset;
        WORD                    YOffset;
        UBYTE                   Depth;
        UBYTE                   ClearMask;
        BOOL                    Border;
        ULONG                   Mask;
        struct CLUTEntry        CLUT[256];

        struct ViewPort *       ViewPort;                                                              /* ViewPort Stuff */
        struct BitMap *         VisibleBitMap;
        struct BitMapExtra *    BitMapExtra;
        struct MinList          BitMapList;
        struct MinList          MemList;

        WORD                    MouseX;
        WORD                    MouseY;                                         /* Sprite Stuff */
        UBYTE                   MouseWidth;
        UBYTE                   MouseHeight;
        UBYTE                   MouseXOffset;
        UBYTE                   MouseYOffset;
        UWORD *                 MouseImage;
        UBYTE                   MousePens[4];
        struct Rectangle        MouseRect;
        UBYTE *                 MouseChunky;
        UWORD *                 MouseRendered;
        UBYTE *                 MouseSaveBuffer;

        ULONG                   ChipData[16];                                                   /* for chip driver needs */
        ULONG                   CardData[16];                                                   /* for card driver needs */

        APTR                    MemorySpaceBase;                                                /* the base address of the board memory address space */
        ULONG                   MemorySpaceSize;                                                /* size of that area */

        APTR                    DoubleBufferList;                                               /* chain of dbinfos being notified on vblanks */

        struct timeval          SyncTime;                                                               /* system time when screen was set up, used for pseudo vblanks */
        ULONG                   SyncPeriod;                                                             /* length of one frame in micros */
        struct MsgPort          SoftVBlankPort;                                         /* MsgPort for software emulation of board interrupt */

        struct MinList          WaitQ;                                                                  /* for WaitTOF and WaitBOVP, all elements will be signaled on VBlank */
        
        LONG                    EssentialFormats;                                      /* these RGBFormats will be used when user does not choose "all"
                                                                                          will be filled by InitBoard() */
        UBYTE *                 MouseImageBuffer;                                      /* rendered to the destination color format */
  /* Additional viewport stuff */
        struct ViewPort *       backViewPort;     /* The view port visible on the screen behind */
        struct BitMap *         backBitMap;       /* Its bitmap */
        struct BitMapExtra *    backExtra;        /* its bitmapExtra */
        WORD                    YSplit;
        ULONG                   MaxPlanarMemory;  /* Size of a bitplane if planar. If left blank, MemorySize>>2 */
        ULONG                   MaxBMWidth;       /* Maximum width of a bitmap */
        ULONG                   MaxBMHeight;      /* Maximum height of a bitmap */
};

/* BoardInfo flags */
/*  0-15: hardware flags */
/* 16-31: user flags */
#define BIB_HARDWARESPRITE               0              /* board has hardware sprite */
#define BIB_NOMEMORYMODEMIX              1              /* board does not support modifying planar bitmaps while displaying chunky and vice versa */
#define BIB_NEEDSALIGNMENT               2              /* bitmaps have to be aligned (not yet supported!) */
#define BIB_CACHEMODECHANGE              3              /* board memory may be set to Imprecise (060) or Nonserialised (040) */
#define BIB_VBLANKINTERRUPT              4              /* board can cause a hardware interrupt on a vertical retrace */
#define BIB_HASSPRITEBUFFER              5              /* board has allocated memory for software sprite image and save buffer */

#define BIB_VGASCREENSPLIT               6              /* has a screen B with fixed screen position for split-screens */

#define BIB_DBLSCANDBLSPRITEY            8              /* hardware sprite y position is doubled on doublescan display modes */
#define BIB_ILACEHALFSPRITEY             9              /* hardware sprite y position is halved on interlace display modes */
#define BIB_ILACEDBLROWOFFSET           10              /* doubled row offset in interlaced display modes needs additional horizontal bit */
#define BIB_INTERNALMODESONLY           11              /* board creates its resolutions and modes automatically and does not support user setting files (UAE) */
#define BIB_FLICKERFIXER                12              /* board can flicker fix Amiga RGB signal */
#define BIB_VIDEOCAPTURE                13              /* board can capture video data to a memory area */
#define BIB_VIDEOWINDOW                 14              /* board can display a second mem area as a pip */
#define BIB_BLITTER                     15              /* board has blitter */

#define BIB_HIRESSPRITE                 16              /* mouse sprite has double resolution */
#define BIB_BIGSPRITE                   17              /* user wants big mouse sprite */
#define BIB_BORDEROVERRIDE              18              /* user wants to override system overscan border prefs */
#define BIB_BORDERBLANK                 19              /* user wants border blanking */
#define BIB_INDISPLAYCHAIN              20              /* board switches Amiga signal */
#define BIB_QUIET                       21              /* not yet implemented */
#define BIB_NOMASKBLITS                 22              /* perform blits without taking care of mask */
#define BIB_NOP2CBLITS                  23              /* use CPU for planar to chunky conversions */
#define BIB_NOBLITTER                   24              /* disable all blitter functions */
#define BIB_SYSTEM2SCREENBLITS          25              /* allow data to be written to screen memory for cpu as blitter source */
#define BIB_GRANTDIRECTACCESS           26              /* all data on the board can be accessed at any time without bi->SetMemoryMode() */

#define BIB_OVERCLOCK                   31              /* enable overclocking for some boards */

#define BIB_IGNOREMASK  BIB_NOMASKBLITS

#define BIF_HARDWARESPRITE              (1<<BIB_HARDWARESPRITE)
#define BIF_NOMEMORYMODEMIX             (1<<BIB_NOMEMORYMODEMIX)
#define BIF_NEEDSALIGNMENT              (1<<BIB_NEEDSALIGNMENT)
#define BIF_CACHEMODECHANGE             (1<<BIB_CACHEMODECHANGE)
#define BIF_VBLANKINTERRUPT             (1<<BIB_VBLANKINTERRUPT)
#define BIF_HASSPRITEBUFFER             (1<<BIB_HASSPRITEBUFFER)
#define BIF_VGASCREENSPLIT              (1<<BIB_VGASCREENSPLIT)
#define BIF_DBLSCANDBLSPRITEY           (1<<BIB_DBLSCANDBLSPRITEY)
#define BIF_ILACEHALFSPRITEY            (1<<BIB_ILACEHALFSPRITEY)
#define BIF_ILACEDBLROWOFFSET           (1<<BIB_ILACEDBLROWOFFSET)
#define BIF_INTERNALMODESONLY           (1<<BIB_INTERNALMODESONLY)
#define BIF_FLICKERFIXER                (1<<BIB_FLICKERFIXER)
#define BIF_VIDEOCAPTURE                (1<<BIB_VIDEOCAPTURE)
#define BIF_VIDEOWINDOW                 (1<<BIB_VIDEOWINDOW)
#define BIF_BLITTER                     (1<<BIB_BLITTER)
#define BIF_HIRESSPRITE                 (1<<BIB_HIRESSPRITE)
#define BIF_BIGSPRITE                   (1<<BIB_BIGSPRITE)
#define BIF_BORDEROVERRIDE              (1<<BIB_BORDEROVERRIDE)
#define BIF_BORDERBLANK                 (1<<BIB_BORDERBLANK)
#define BIF_INDISPLAYCHAIN              (1<<BIB_INDISPLAYCHAIN)
#define BIF_QUIET                       (1<<BIB_QUIET)
#define BIF_NOMASKBLITS                 (1<<BIB_NOMASKBLITS)
#define BIF_NOP2CBLITS                  (1<<BIB_NOP2CBLITS)
#define BIF_NOBLITTER                   (1<<BIB_NOBLITTER)
#define BIF_SYSTEM2SCREENBLITS          (1<<BIB_SYSTEM2SCREENBLITS)
#define BIF_GRANTDIRECTACCESS           (1<<BIB_GRANTDIRECTACCESS)
#define BIF_OVERCLOCK                   (1<<BIB_OVERCLOCK)

#define BIF_IGNOREMASK  BIF_NOMASKBLITS

/* write errors, continued for historical reasons... :-) */
#define BIB_NOC2PBLITS                  BIB_NOP2CBLITS
#define BIF_NOC2PBLITS                  BIF_NOP2CBLITS

/************************************************************************/

struct CardBase {
        struct Library          LibBase;
        UBYTE                   Flags;
        UBYTE                   pad;

        struct ExecBase *       ExecBase;
        struct Library *        ExpansionBase;

        APTR                    SegList;
        char *                  Name;
};

struct ChipBase {
        struct Library          LibBase;
        UBYTE                   Flags;
        UBYTE                   pad;

        struct ExecBase *       ExecBase;

        APTR                    SegList;
};

/************************************************************************/
/* private Tags */
#define P96BD_BoardType                 (P96BD_Dummy+0x101)
#define P96BD_ChipType                  (P96BD_Dummy+0x102)
#define P96BD_DACType                   (P96BD_Dummy+0x103)
#define P96BD_CurrentScreenBitMap       (P96BD_Dummy+0x104)

/************************************************************************/
#endif