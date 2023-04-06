#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>

#include <proto/exec.h>

#include "emu68-vc4.h"
#include "vc4.h"
#include "boardinfo.h"
#include "mbox.h"

static int mitchell_netravali(double x, double b, double c)
{
    double k;

    if (x < 0)
        x = -x;
    
    if (x < 1) {
        k = (12.0 - 9.0 * b - 6.0 * c) * x * x * x + (-18.0 + 12.0 * b + 6.0 * c) * x * x + (6.0 - 2.0 * b);
    }
    else if (x < 2) {
        k = (-b - 6.0 * c) * x * x * x + (6.0 * b + 30.0 * c) * x * x + (-12.0 * b - 48.0 * c) * x + 8.0 * b + 24.0 * c;
    }
    else
        k = 0;
    
    k = 255.0 * k / 6.0 + 0.5;

    return (int)k;
}

int unity_kernel = 0xfd0;
int kernel_start = 0xff0;

int compute_scaling_kernel(uint32_t *dlist_memory, double b, double c)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    uint32_t half_kernel[6] = {0, 0, 0, 0, 0, 0};

    for (int i=0; i < 16; i++) {
        int val = mitchell_netravali(2.0 - (double)i / 7.5, b, c);
#if 0
        LONG args[] = {
            i, val
        };
        RawDoFmt("[vc4] Kernel[%ld] = %ld\n", args, (APTR)putch, NULL);
#endif
        half_kernel[i / 3] |= (val & 0x1ff) << (9 * (i % 3));
    }
    half_kernel[5] |= half_kernel[5] << 9;

    for (int i=0; i<11; i++) {
        if (i < 6) {
            dlist_memory[kernel_start + i] = LE32(half_kernel[i]);
        } else {
            dlist_memory[kernel_start + i] = LE32(half_kernel[11 - i - 1]);
        }
    }

    return kernel_start;
}

int compute_nearest_neighbour_kernel(uint32_t *dlist_memory)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    uint32_t half_kernel[6] = {0, 0, 0, 0, 0, 0};

    for (int i=0; i < 16; i++) {
        int val = i < 8 ? 0 : 255;
#if 0
        LONG args[] = {
            i, val
        };
        RawDoFmt("[vc4] Kernel[%ld] = %ld\n", args, (APTR)putch, NULL);
#endif
        half_kernel[i / 3] |= (val & 0x1ff) << (9 * (i % 3));
    }
    half_kernel[5] |= half_kernel[5] << 9;

    for (int i=0; i<11; i++) {
        if (i < 6) {
            dlist_memory[kernel_start + i] = LE32(half_kernel[i]);
        } else {
            dlist_memory[kernel_start + i] = LE32(half_kernel[11 - i - 1]);
        }
    }

    return kernel_start;
}

UWORD CalculateBytesPerRow(struct BoardInfo *b asm("a0"), UWORD width asm("d0"), RGBFTYPE format asm("d7"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    if (!b)
        return 0;

    UWORD pitch = width;

    if (0)
    {
        bug("[VC4] CalculateBytesPerRow pitch %ld, format %lx\n", pitch, format);
    }
    

    switch(format) {
        case RGBFB_CLUT:
            return pitch;
        default:
            return 128;
        case RGBFB_R5G6B5PC: case RGBFB_R5G5B5PC:
        case RGBFB_R5G6B5: case RGBFB_R5G5B5:
        case RGBFB_B5G6R5PC: case RGBFB_B5G5R5PC:
            return (width * 2);
        case RGBFB_R8G8B8: case RGBFB_B8G8R8:
            // Should actually return width * 3, but I'm not sure if
            // the Pi VC supports 24-bit color formats.
            // P96 will sometimes magically pad these to 32-bit anyway.
            return (width * 3);
        case RGBFB_B8G8R8A8: case RGBFB_R8G8B8A8:
        case RGBFB_A8B8G8R8: case RGBFB_A8R8G8B8:
            return (width * 4);
    }
}

void SetDAC(struct BoardInfo *b asm("a0"), RGBFTYPE format asm("d7"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    
    if (0)
        bug("[VC4] SetDAC\n");
    // Used to set the color format of the video card's RAMDAC.
    // This needs no handling, since the PiStorm doesn't really have a RAMDAC or a video card chipset.
}


void SetGC(struct BoardInfo *b asm("a0"), struct ModeInfo *mode_info asm("a1"), BOOL border asm("d0"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Size dim;
    int need_switch = 0;


    if (b->ModeInfo != mode_info) {
        need_switch = 1;
        b->ModeInfo = mode_info;
    }

    dim.width = mode_info->Width;
    dim.height = mode_info->Height;
    
    if (0)
    {
        bug("[VC4] SetGC %ld x %ld x %ld\n", dim.width, dim.height, mode_info->Depth);
    }

    if (need_switch) {
        VC4Base->vc4_LastPanning.lp_Addr = NULL;
        //init_display(dim, mode_info->Depth, &VC4Base->vc4_Framebuffer, &VC4Base->vc4_Pitch, VC4Base);
    }
}

UWORD SetSwitch(struct BoardInfo *b asm("a0"), UWORD enabled asm("d0"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    if (0)
    {
        bug("[VC4] SetSwitch %ld\n", enabled);
    }

    if (VC4Base->vc4_Enabled != enabled) {
        VC4Base->vc4_Enabled = enabled;

        switch(enabled) {
            case 0:
                blank_screen(1, VC4Base);
                break;
            default:
                blank_screen(0, VC4Base);
                break;
        }
    }
    
    return 1 - enabled;
}


static const ULONG mode_table[] = {
    [RGBFB_A8R8G8B8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_RGBA),
    [RGBFB_A8B8G8R8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_BGRA),
    [RGBFB_B8G8R8A8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR),
    [RGBFB_R8G8B8A8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGBA8888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ARGB),

    [RGBFB_R8G8B8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XBGR),
    [RGBFB_B8G8R8] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB),

    [RGBFB_R5G6B5PC] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB),
    [RGBFB_R5G5B5PC] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB555) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB),
    
    [RGBFB_R5G6B5] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB),
    [RGBFB_R5G5B5] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB555) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB),
    
    [RGBFB_B5G6R5PC] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XBGR),
    [RGBFB_B5G5R5PC] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB555) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XBGR),

    [RGBFB_CLUT] = CONTROL_FORMAT(HVS_PIXEL_FORMAT_PALETTE) | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XBGR)
};

int AllocSlot(UWORD size, struct VC4Base *VC4Base)
{
    int ret = VC4Base->vc4_FreePlane;
    int next_free = VC4Base->vc4_FreePlane + size;

    if (next_free >= 0x300)
    {
        ret = 0;
        next_free = ret + size;
    }

    VC4Base->vc4_FreePlane = next_free;

    return ret;
}

void SetPanning (struct BoardInfo *b asm("a0"), UBYTE *addr asm("a1"), UWORD width asm("d0"), WORD x_offset asm("d1"), WORD y_offset asm("d2"), RGBFTYPE format asm("d7"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    int unity = 0;
    ULONG scale_x = 0;
    ULONG scale_y = 0;
    ULONG scale = 0;
    ULONG recip_x = 0;
    ULONG recip_y = 0;
    UWORD offset_x = 0;
    UWORD offset_y = 0;
    ULONG calc_width = 0;
    ULONG calc_height = 0;
    ULONG sprite_width = 0;
    ULONG sprite_height = 0;
    ULONG bytes_per_row = CalculateBytesPerRow(b, width, format);
    ULONG bytes_per_pix = bytes_per_row / width;
    UWORD pos = 0;
    int offset_only = 0;

    if (0) {
        bug("[VC4] SetPanning %lx %ld %ld %ld %lx\n", addr, width, x_offset, y_offset, format);
    }

    if (VC4Base->vc4_LastPanning.lp_Addr != NULL && 
        width == VC4Base->vc4_LastPanning.lp_Width &&
        format == VC4Base->vc4_LastPanning.lp_Format)
    {
        if (addr == VC4Base->vc4_LastPanning.lp_Addr && x_offset == VC4Base->vc4_LastPanning.lp_X && y_offset == VC4Base->vc4_LastPanning.lp_Y) {
            if (0) {
                bug("[VC4] same panning as before. Skipping now\n");
            }
            return;
        }

        offset_only = 1;
    }

    VC4Base->vc4_LastPanning.lp_Addr = addr;
    VC4Base->vc4_LastPanning.lp_Width = width;
    VC4Base->vc4_LastPanning.lp_X = x_offset;
    VC4Base->vc4_LastPanning.lp_Y = y_offset;
    VC4Base->vc4_LastPanning.lp_Format = format;

    if (format != RGBFB_CLUT &&
        b->ModeInfo->Width == VC4Base->vc4_DispSize.width &&
        b->ModeInfo->Height == VC4Base->vc4_DispSize.height)
    {
        unity = 1;
        sprite_width = MAXSPRITEWIDTH;
        sprite_height = MAXSPRITEHEIGHT;

        VC4Base->vc4_ScaleX = 0x10000;
        VC4Base->vc4_ScaleY = 0x10000;
        VC4Base->vc4_OffsetX = 0;
        VC4Base->vc4_OffsetY = 0;
    }
    else
    {
        ULONG factor_y = (b->ModeInfo->Flags & GMF_DOUBLESCAN) ? 0x20000 : 0x10000;
        scale_x = 0x10000 * b->ModeInfo->Width / VC4Base->vc4_DispSize.width;
        scale_y = factor_y * b->ModeInfo->Height / VC4Base->vc4_DispSize.height;

        recip_x = 0xffffffff / scale_x;
        recip_y = 0xffffffff / scale_y;

        // Select larger scaling factor from X and Y, but it need to fit
        if (((factor_y * b->ModeInfo->Height) / scale_x) > VC4Base->vc4_DispSize.height) {
            scale = scale_y;
        }
        else {
            scale = scale_x;
        }

        VC4Base->vc4_ScaleX = scale;
        VC4Base->vc4_ScaleY = (b->ModeInfo->Flags & GMF_DOUBLESCAN) ? scale >> 1 : scale;

        calc_width = (0x10000 * b->ModeInfo->Width) / scale;
        calc_height = (factor_y * b->ModeInfo->Height) / scale;

        sprite_width = (0x10000 * MAXSPRITEWIDTH) / scale;
        sprite_height = (factor_y * MAXSPRITEHEIGHT) / scale;

        offset_x = (VC4Base->vc4_DispSize.width - calc_width) >> 1;
        offset_y = (VC4Base->vc4_DispSize.height - calc_height) >> 1;

        VC4Base->vc4_OffsetX = offset_x;
        VC4Base->vc4_OffsetY = offset_y;

        if (0)
            bug("[VC4] Selected scale: %08lx (X: %08lx, Y: %08lx, 1/X: %08lx, 1/Y: %08lx)\n"
                "[VC4] Scaled size: %ld x %ld, offset X %ld, offset Y %ld\n", scale, scale_x, scale_y, recip_x, recip_y,
                calc_width, calc_height, offset_x, offset_y);
    }

    volatile ULONG *displist = (ULONG *)0xf2402000;
   
    if (unity) {
        if (offset_only) {
            pos = VC4Base->vc4_ActivePlane;
            displist[pos + 4] = LE32(0xc0000000 | (ULONG)addr + y_offset * bytes_per_row + x_offset * bytes_per_pix);
            if (VC4Base->vc4_SpriteVisible)
                SetSpritePosition(b, VC4Base->vc4_MouseX, VC4Base->vc4_MouseY, format);
        }
        else {
            pos = AllocSlot(8 + 18 + 4, VC4Base);
            int cnt = pos + 1;

            VC4Base->vc4_PlaneCoord = &displist[cnt];
            displist[cnt++] = LE32(POS0_X(offset_x) | POS0_Y(offset_y) | POS0_ALPHA(0xff));
            
            displist[cnt++] = LE32(POS2_H(b->ModeInfo->Height) | POS2_W(b->ModeInfo->Width) | (1 << 30));
            displist[cnt++] = LE32(0xdeadbeef);
            displist[cnt++] = LE32(0xc0000000 | (ULONG)addr + y_offset * bytes_per_row + x_offset * bytes_per_pix);
            displist[cnt++] = LE32(0xdeadbeef);
            displist[cnt++] = LE32(bytes_per_row);

            displist[pos] = LE32(
                CONTROL_VALID
                | CONTROL_WORDS(cnt - pos)
                | CONTROL_UNITY
                | mode_table[format]);

            VC4Base->vc4_PlaneScalerX = NULL;
            VC4Base->vc4_PlaneScalerY = NULL;

            int mouse_pos = cnt;
            cnt = mouse_pos + 1;

            VC4Base->vc4_MouseCoord = &displist[cnt];
            displist[cnt++] = LE32( POS0_X(offset_x + VC4Base->vc4_MouseX - x_offset) |
                                    POS0_Y(offset_y + VC4Base->vc4_MouseY - y_offset) | POS0_ALPHA(0xff));
            displist[cnt++] = LE32(POS1_H(sprite_height) | POS1_W(sprite_width));
            displist[cnt++] = LE32(POS2_H(MAXSPRITEHEIGHT) | POS2_W(MAXSPRITEWIDTH) | (SCALER_POS2_ALPHA_MODE_PIPELINE << SCALER_POS2_ALPHA_MODE_SHIFT));
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            displist[cnt++] = LE32(0xc0000000 | (ULONG)VC4Base->vc4_SpriteShape);
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            // Write pitch
            displist[cnt++] = LE32(MAXSPRITEWIDTH);

            int clut_off = cnt;
            displist[cnt++] = LE32(0xc0000000 | (0x300 << 2));

            // LMB address - just behind LMB of main plane
            displist[cnt++] = LE32(16 * b->ModeInfo->Width / 2);

            // Write PPF Scaling
            displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            if (b->ModeInfo->Flags & GMF_DOUBLESCAN)
                displist[cnt++] = LE32(((scale << 7) & ~0xff) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            else
                displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            displist[cnt++] = LE32(0); // Scratch written by HVS

            // Write scaling kernel offset in dlist
            displist[cnt++] = LE32(unity_kernel);
            displist[cnt++] = LE32(unity_kernel);
            displist[cnt++] = LE32(unity_kernel);
            displist[cnt++] = LE32(unity_kernel);

            displist[mouse_pos] = LE32(
                CONTROL_VALID               |
                CONTROL_WORDS(cnt-mouse_pos)    |
                0x01800 | 
                mode_table[RGBFB_CLUT]
            );

            displist[cnt++] = LE32(0x80000000);

            displist[clut_off] = LE32(0xc0000000 | (cnt << 2));

            displist[cnt++] = LE32(0x00000000);
            VC4Base->vc4_MousePalette = &displist[cnt];
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[0]);
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[1]);
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[2]);
        }
    } else {
        if (offset_only) {
            pos = VC4Base->vc4_ActivePlane;
            displist[pos + 5] = LE32(0xc0000000 | (ULONG)addr + y_offset * bytes_per_row + x_offset * bytes_per_pix);
            if (VC4Base->vc4_SpriteVisible)
                SetSpritePosition(b, VC4Base->vc4_MouseX, VC4Base->vc4_MouseY, format);
        }
        else 
        {
            pos = AllocSlot(2*18 + 4, VC4Base);
            int cnt = pos + 1;

            VC4Base->vc4_PlaneCoord = &displist[cnt];
            displist[cnt++] = LE32(POS0_X(offset_x) | POS0_Y(offset_y) | POS0_ALPHA(0xff));
            displist[cnt++] = LE32(POS1_H(calc_height) | POS1_W(calc_width));
            displist[cnt++] = LE32(POS2_H(b->ModeInfo->Height) | POS2_W(b->ModeInfo->Width) | (SCALER_POS2_ALPHA_MODE_FIXED << SCALER_POS2_ALPHA_MODE_SHIFT));
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            displist[cnt++] = LE32(0xc0000000 | (ULONG)addr + y_offset * bytes_per_row + x_offset * bytes_per_pix);
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            // Write pitch
            displist[cnt++] = LE32(bytes_per_row);

            // Palette mode - offset of palette placed in dlist
            if (format == RGBFB_CLUT) {
                displist[cnt++] = LE32(0xc0000000 | (0x300 << 2));
            }

            // LMB address
            displist[cnt++] = LE32(0);

            // Write PPF Scaling
            VC4Base->vc4_PlaneScalerX = &displist[cnt];
            VC4Base->vc4_PlaneScalerY = &displist[cnt+1];

            displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            if (b->ModeInfo->Flags & GMF_DOUBLESCAN)
                displist[cnt++] = LE32(((scale << 7) & ~0xff) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            else
                displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            displist[cnt++] = LE32(0); // Scratch written by HVS

            // Write scaling kernel offset in dlist
            VC4Base->vc4_Kernel = &displist[cnt];
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);

            displist[pos] = LE32(
                CONTROL_VALID             |
                CONTROL_WORDS(cnt-pos)    |
                0x01800 | 
                mode_table[format]
            );

            int mouse_pos = cnt;
            cnt = mouse_pos + 1;

            VC4Base->vc4_MouseCoord = &displist[cnt];
            displist[cnt++] = LE32( POS0_X(offset_x + 0x10000 * (VC4Base->vc4_MouseX - x_offset) / VC4Base->vc4_ScaleX) |
                                    POS0_Y(offset_y + 0x10000 * (VC4Base->vc4_MouseY - y_offset) / VC4Base->vc4_ScaleY) | POS0_ALPHA(0xff));
            displist[cnt++] = LE32(POS1_H(sprite_height) | POS1_W(sprite_width));
            displist[cnt++] = LE32(POS2_H(MAXSPRITEHEIGHT) | POS2_W(MAXSPRITEWIDTH) | (SCALER_POS2_ALPHA_MODE_PIPELINE << SCALER_POS2_ALPHA_MODE_SHIFT));
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            displist[cnt++] = LE32(0xc0000000 | (ULONG)VC4Base->vc4_SpriteShape);
            displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

            // Write pitch
            displist[cnt++] = LE32(MAXSPRITEWIDTH);

            int clut_off = cnt;
            displist[cnt++] = LE32(0xc0000000 | (0x300 << 2));

            // LMB address - just behind LMB of main plane
            displist[cnt++] = LE32(16 * b->ModeInfo->Width / 2);

            // Write PPF Scaling
            displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            if (b->ModeInfo->Flags & GMF_DOUBLESCAN)
                displist[cnt++] = LE32(((scale << 7) & ~0xff) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            else
                displist[cnt++] = LE32((scale << 8) | VC4Base->vc4_Scaler | VC4Base->vc4_Phase);
            displist[cnt++] = LE32(0); // Scratch written by HVS

            // Write scaling kernel offset in dlist
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);
            displist[cnt++] = LE32(kernel_start);

            displist[mouse_pos] = LE32(
                CONTROL_VALID               |
                CONTROL_WORDS(cnt-mouse_pos)    |
                0x01800 | 
                mode_table[RGBFB_CLUT]
            );

            displist[cnt++] = LE32(0x80000000);

            displist[clut_off] = LE32(0xc0000000 | (cnt << 2));

            displist[cnt++] = LE32(0x00000000);
            VC4Base->vc4_MousePalette = &displist[cnt];
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[0]);
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[1]);
            displist[cnt++] = LE32(VC4Base->vc4_SpriteColors[2]);
#if 0
            for (int i=pos; i < cnt; i++) {
                ULONG args[] = {
                    i, LE32(displist[i])
                };

                RawDoFmt("[vc4] dlist[%ld] = %08lx\n", args, (APTR)putch, NULL);
            }
#endif
        }
    }

    if (pos != VC4Base->vc4_ActivePlane)
    {
        volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);

        // Wait for vertical blank before updating the display list
        do { asm volatile("nop"); } while((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height);

        *(volatile uint32_t *)0xf2400024 = LE32(pos);
        VC4Base->vc4_ActivePlane = pos;
    }
}


void SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num)) {
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    volatile uint32_t *displist = (uint32_t *)0xf2402000;

    // Sets the color components of X color components for 8-bit paletted display modes.
    if (!b->CLUT)
        return;
    
    if (0)
    {
        bug("[VC4] SetColorArray %ld %ld\n", start, num);
    }

    int j = start + num;
    
    for(int i = start; i < j; i++) {
        unsigned long xrgb = 0xff000000 | (b->CLUT[i].Blue) | (b->CLUT[i].Green << 8) | (b->CLUT[i].Red << 16);
        displist[0x300 + i] = LE32(xrgb);
    }
}


APTR CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format)) {
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    if (0)
    {
        bug("[VC4] CalculateMemory %lx %lx\n", addr, format);
    }

    return (APTR)addr;
}


enum fake_rgbftypes {
    RGBF_8BPP_CLUT,
    RGBF_24BPP_RGB,
    RGBF_24BPP_BGR,
    RGBF_16BPP_RGB565_PC,
    RGBF_16BPP_RGB555_PC,
	RGBF_32BPP_ARGB,
    RGBF_32BPP_ABGR,
	RGBF_32BPP_RGBA,
    RGBF_32BPP_BGRA,
    RGBF_16BPP_RGB565,
    RGBF_16BPP_RGB555,
    RGBF_16BPP_BGR565_PC,
    RGBF_16BPP_BGR555_PC,
    RGBF_YUV_422_0,  // (Actually 4:2:0?) Just a duplicate of RGBF_YUV_422?
    RGBF_YUV_411,    // No, these are 4:2:0
    RGBF_YUV_411_PC, // No, these are 4:2:0
    RGBF_YUV_422,
    RGBF_YUV_422_PC,
    RGBF_YUV_422_PLANAR,
    RGBF_YUV_422_PLANAR_PC,
};
#define BIP(a) (1 << a)

ULONG GetCompatibleFormats (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    if (0)
    {
        bug("[VC4] GetCompatibleFormats %lx\n", format);
    }
    //return BIP(RGBF_8BPP_CLUT) | BIP(RGBF_24BPP_RGB) | BIP(RGBF_24BPP_BGR) | BIP(RGBF_32BPP_ARGB) | BIP(RGBF_32BPP_ABGR) | BIP(RGBF_32BPP_RGBA) | BIP(RGBF_32BPP_BGRA);
    return 0xFFFFFFFF;
}

//static int display_enabled = 0;
UWORD SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    
    if (0)
    {
        bug("[VC4] SetDisplay %ld\n", enabled);
    }
    if (enabled) {
        blank_screen(0, VC4Base);
    } else {
        blank_screen(1, VC4Base);
    }

    return 1;
}


LONG ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format)) {

    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    
    if (0)
    {
        bug("[VC4] ResolvePixelClock %lx %ld %lx\n", mode_info, pixel_clock, format);
    }

    ULONG clock = mode_info->HorTotal * mode_info->VerTotal * VC4Base->vc4_VertFreq;

    if (b->ModeInfo->Flags & GMF_DOUBLESCAN)
        clock <<= 1;

    mode_info->PixelClock = clock;
    mode_info->pll1.Clock = 0;
    mode_info->pll2.ClockDivide = 1;

    return 0;
}

ULONG GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format)) {
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    
    ULONG clock = mode_info->HorTotal * mode_info->VerTotal * VC4Base->vc4_VertFreq;

    if (b->ModeInfo->Flags & GMF_DOUBLESCAN)
        clock <<= 1;

    return clock;
}

// None of these five really have to do anything.
void SetClock (__REGA0(struct BoardInfo *b)) {
}
void SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format)) {
}

void SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask)) {
}

void SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask)) {
}

void SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane)) {
}

void SetSprite (__REGA0(struct BoardInfo *b), __REGD0(BOOL enable), __REGD7(RGBFTYPE format))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;

    VC4Base->vc4_SpriteVisible = enable;

    if (enable) {
        LONG _x;
        LONG _y;

        if (VC4Base->vc4_ScaleX)
            _x = 0x10000 * VC4Base->vc4_MouseX / VC4Base->vc4_ScaleX;
        else
            _x = VC4Base->vc4_MouseX;

        if (VC4Base->vc4_ScaleY)
            _y = 0x10000 * VC4Base->vc4_MouseY / VC4Base->vc4_ScaleX;
        else
            _y = VC4Base->vc4_MouseY;

        if (VC4Base->vc4_MouseCoord) {
            VC4Base->vc4_MouseCoord[0] = LE32(POS0_X(_x) | POS0_Y(_y) | POS0_ALPHA(0xff));
        }
    }
    else
    {
        if (VC4Base->vc4_MouseCoord) {
            VC4Base->vc4_MouseCoord[0] = LE32(POS0_X(-1) | POS0_Y(-1) | POS0_ALPHA(0xff));
        }
    }
}

void SetSpritePosition (__REGA0(struct BoardInfo *b), __REGD0(WORD x), __REGD1(WORD y), __REGD7(RGBFTYPE format))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;

/*
    x = b->MouseX - b->XOffset;
    y = b->MouseY - b->YOffset;
*/

    VC4Base->vc4_MouseX = x;
    VC4Base->vc4_MouseY = y;

    x -= VC4Base->vc4_LastPanning.lp_X;
    y -= VC4Base->vc4_LastPanning.lp_Y;

    LONG _x;
    LONG _y;

    if (VC4Base->vc4_ScaleX)
        _x = 0x10000 * x / VC4Base->vc4_ScaleX;
    else
        _x = x;

    if (VC4Base->vc4_ScaleY)
        _y = 0x10000 * y / VC4Base->vc4_ScaleY;
    else
        _y = y;

    _x += VC4Base->vc4_OffsetX;
    _y += VC4Base->vc4_OffsetY;

    if (VC4Base->vc4_MouseCoord) {   
        VC4Base->vc4_MouseCoord[0] = LE32(POS0_X(_x) | POS0_Y(_y) | POS0_ALPHA(0xff));
    }
}


void SetSpriteImage (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    struct ExecBase *SysBase = *(struct ExecBase **)4;

    for (int i=0; i < MAXSPRITEWIDTH * MAXSPRITEHEIGHT; i++)
        VC4Base->vc4_SpriteShape[i] = 0;

    if ((b->Flags & (BIF_HIRESSPRITE | BIF_BIGSPRITE)) == 0)
    {
        UWORD *data = b->MouseImage;
        data += 2;

        for (int y=0; y < b->MouseHeight; y++) {
            UWORD p0 = *data++;
            UWORD p1 = *data++;
            UWORD mask = 0x8000;
            for (int x=0; x < 16; x++) {
                UBYTE pix = 0;
                if (p0 & mask) pix |= 1;
                if (p1 & mask) pix |= 2;
                VC4Base->vc4_SpriteShape[y * MAXSPRITEWIDTH + x] = pix;
                mask = mask >> 1;
            }
        }
    }
    else if (b->Flags & BIF_BIGSPRITE) {
        UWORD *data = b->MouseImage;
        data += 2;

        for (int y=0; y < b->MouseHeight; y++) {
            UWORD p0 = *data++;
            UWORD p1 = *data++;
            UWORD mask = 0x8000;
            for (int x=0; x < 16; x++) {
                UBYTE pix = 0;
                if (p0 & mask) pix |= 1;
                if (p1 & mask) pix |= 2;
                VC4Base->vc4_SpriteShape[2 * y * MAXSPRITEWIDTH + 2*x] = pix;
                VC4Base->vc4_SpriteShape[2 * y * MAXSPRITEWIDTH + 2*x + 1] = pix;
                VC4Base->vc4_SpriteShape[(2 * y + 1) * MAXSPRITEWIDTH + 2*x] = pix;
                VC4Base->vc4_SpriteShape[(2 * y + 1) * MAXSPRITEWIDTH + 2*x + 1] = pix;
                mask = mask >> 1;
            }
        }
    }
    else if (b->Flags & BIF_HIRESSPRITE) {
        ULONG *data = (ULONG*)b->MouseImage;
        data += 2;

        for (int y=0; y < b->MouseHeight; y++) {
            ULONG p0 = *data++;
            ULONG p1 = *data++;
            ULONG mask = 0x80000000;
            for (int x=0; x < 32; x++) {
                UBYTE pix = 0;
                if (p0 & mask) pix |= 1;
                if (p1 & mask) pix |= 2;
                VC4Base->vc4_SpriteShape[y * MAXSPRITEWIDTH + x] = pix;
                mask = mask >> 1;
            }
        }
    }

    CacheClearE(VC4Base->vc4_SpriteShape, MAXSPRITEHEIGHT * MAXSPRITEWIDTH, CACRF_ClearD);
}

void SetSpriteColor (__REGA0(struct BoardInfo *b), __REGD0(UBYTE idx), __REGD1(UBYTE R), __REGD2(UBYTE G), __REGD3(UBYTE B), __REGD7(RGBFTYPE format))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    if (idx < 3) {
        VC4Base->vc4_SpriteColors[idx] = (VC4Base->vc4_SpriteAlpha << 24) | (R << 16) | (G << 8) | B;
        if (VC4Base->vc4_MousePalette) {
            VC4Base->vc4_MousePalette[idx] = LE32(VC4Base->vc4_SpriteColors[idx]);
        }
    }
}

ULONG GetVBeamPos(struct BoardInfo *b asm("a0"))
{
    volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);
    ULONG vbeampos = LE32(*stat) & 0xfff;

    return vbeampos;
}

void WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle)) {
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);

    // Wait until current vbeampos is lower than the one obtained above
    do { asm volatile("nop"); } while((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height);
}

BOOL GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL expected))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    volatile ULONG *stat = (ULONG*)(0xf2400000 + SCALER_DISPSTAT1);
    
    // Ignore expected value
    (void)expected;

    // If picture is in visible area, then return 0, if in vblank return 1
    if ((LE32(*stat) & 0xfff) != VC4Base->vc4_DispSize.height)
        return 0;
    else
        return 1;
}