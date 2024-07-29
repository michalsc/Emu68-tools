/*
    Copyright © 2021 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/errors.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/devicetree.h>

#include <libraries/configregs.h>
#include <libraries/configvars.h>

#include <stdint.h>

#include "unicam.h"
#include "videocore.h"
#include "smoothing.h"


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

/* Unicam DisplayList */
void VC4_ConstructUnicamDL(struct UnicamBase *UnicamBase, ULONG kernel)
{
    int unity = 0;
    ULONG scale_x = 0;
    ULONG scale_y = 0;
    ULONG scale = 0;
    ULONG recip_x = 0;
    ULONG recip_y = 0;
    ULONG calc_width = 0;
    ULONG calc_height = 0;
    ULONG offset_x = 0;
    ULONG offset_y = 0;

    ULONG cnt = 0x300; // Initial pointer to UnicamDL

    volatile ULONG *displist = (ULONG *)((ULONG)UnicamBase->u_PeriphBase + 0x00402000);

    if (UnicamBase->u_Size.width == UnicamBase->u_DisplaySize.width &&
        UnicamBase->u_Size.height == UnicamBase->u_DisplaySize.height && UnicamBase->u_Aspect == 1000)
    {
        unity = 1;
    }
    else
    {
        scale_x = 0x10000 * ((UnicamBase->u_Size.width * UnicamBase->u_Aspect) / 1000) / UnicamBase->u_DisplaySize.width;
        scale_y = 0x10000 * UnicamBase->u_Size.height / UnicamBase->u_DisplaySize.height;

        recip_x = 0xffffffff / scale_x;
        recip_y = 0xffffffff / scale_y;

        // Select larger scaling factor from X and Y, but it need to fit
        if (((0x10000 * UnicamBase->u_Size.height) / scale_x) > UnicamBase->u_DisplaySize.height) {
            scale = scale_y;
        }
        else {
            scale = scale_x;
        }

        if (UnicamBase->u_Integer)
        {
            scale = 0x10000 / (ULONG)(0x10000 / scale);
        }

        scale_x = scale * 1000 / UnicamBase->u_Aspect;
        scale_y = scale;

        calc_width = (0x10000 * UnicamBase->u_Size.width) / scale_x;
        calc_height = (0x10000 * UnicamBase->u_Size.height) / scale_y;

        offset_x = (UnicamBase->u_DisplaySize.width - calc_width) >> 1;
        offset_y = (UnicamBase->u_DisplaySize.height - calc_height) >> 1;
    }

    ULONG startAddress = (ULONG)UnicamBase->u_ReceiveBuffer;
    startAddress += UnicamBase->u_Offset.x * (UnicamBase->u_BPP / 8);
    startAddress += UnicamBase->u_Offset.y * UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8);

    if (unity)
    {
        /* Unity scaling is simple, reserve less space for display list */
        cnt -= 8;

        UnicamBase->u_UnicamDL = cnt;

        /* Set control reg */
        displist[cnt++] = LE32(
            CONTROL_VALID
            | CONTROL_WORDS(7)
            | CONTROL_UNITY
            | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB)
        );

        if (UnicamBase->u_BPP == 16)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565));
        else if (UnicamBase->u_BPP == 24)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888));

        /* Center it on the screen */
        displist[cnt++] = LE32(POS0_X(offset_x) | POS0_Y(offset_y) | POS0_ALPHA(0xff));
        displist[cnt++] = LE32(POS2_H(UnicamBase->u_Size.height) | POS2_W(UnicamBase->u_Size.width) | (1 << 30));
        displist[cnt++] = LE32(0xdeadbeef);

        /* Set address */
        displist[cnt++] = LE32(0xc0000000 | (ULONG)startAddress);
        displist[cnt++] = LE32(0xdeadbeef);

        /* Pitch is full width, always */
        displist[cnt++] = LE32(UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8));

        /* Done */
        displist[cnt++] = LE32(0x80000000);
    }
    else
    {
        cnt -= 17;
        
        UnicamBase->u_UnicamDL = cnt;

        /* Set control reg */
        displist[cnt++] = LE32(
            CONTROL_VALID
            | CONTROL_WORDS(16)
            | 0x01800 
            | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB)
        );

        if (UnicamBase->u_BPP == 16)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565));
        else if (UnicamBase->u_BPP == 24)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888));

        /* Center plane on the screen */
        displist[cnt++] = LE32(POS0_X(offset_x) | POS0_Y(offset_y) | POS0_ALPHA(0xff));
        displist[cnt++] = LE32(POS1_H(calc_height) | POS1_W(calc_width));
        displist[cnt++] = LE32(POS2_H(UnicamBase->u_Size.height) | POS2_W(UnicamBase->u_Size.width) |
                               (SCALER_POS2_ALPHA_MODE_FIXED << SCALER_POS2_ALPHA_MODE_SHIFT));
        displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

        /* Set address and pitch */
        displist[cnt++] = LE32(0xc0000000 | startAddress);
        displist[cnt++] = LE32(0xdeadbeef);

        /* Pitch is full width, always */
        displist[cnt++] = LE32(UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8));

        /* LMB address */
        displist[cnt++] = LE32(0);

        /* Set PPF Scaler */
        displist[cnt++] = LE32((scale_x << 8) | (UnicamBase->u_Scaler << 30) | UnicamBase->u_Phase);
        displist[cnt++] = LE32((scale_y << 8) | (UnicamBase->u_Scaler << 30) | UnicamBase->u_Phase);
        displist[cnt++] = LE32(0); // Scratch written by HVS

        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);

        /* Done */
        displist[cnt++] = LE32(0x80000000);
    }
}

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

void VC6_ConstructUnicamDL(struct UnicamBase *UnicamBase, ULONG kernel)
{
    int unity = 0;
    ULONG scale_x = 0;
    ULONG scale_y = 0;
    ULONG scale = 0;
    ULONG recip_x = 0;
    ULONG recip_y = 0;
    ULONG calc_width = 0;
    ULONG calc_height = 0;
    ULONG offset_x = 0;
    ULONG offset_y = 0;

    ULONG cnt = 0x300; // Initial pointer to UnicamDL

    volatile ULONG *displist = (ULONG *)((ULONG)UnicamBase->u_PeriphBase + 0x00404000);

    if (UnicamBase->u_Size.width == UnicamBase->u_DisplaySize.width &&
        UnicamBase->u_Size.height == UnicamBase->u_DisplaySize.height && UnicamBase->u_Aspect == 1000)
    {
        unity = 1;
    }
    else
    {
        scale_x = 0x10000 * ((UnicamBase->u_Size.width * UnicamBase->u_Aspect) / 1000) / UnicamBase->u_DisplaySize.width;
        scale_y = 0x10000 * UnicamBase->u_Size.height / UnicamBase->u_DisplaySize.height;

        recip_x = 0xffffffff / scale_x;
        recip_y = 0xffffffff / scale_y;

        // Select larger scaling factor from X and Y, but it need to fit
        if (((0x10000 * UnicamBase->u_Size.height) / scale_x) > UnicamBase->u_DisplaySize.height) {
            scale = scale_y;
        }
        else {
            scale = scale_x;
        }

        if (UnicamBase->u_Integer)
        {
            scale = 0x10000 / (ULONG)(0x10000 / scale);
        }

        scale_x = scale * 1000 / UnicamBase->u_Aspect;
        scale_y = scale;

        calc_width = (0x10000 * UnicamBase->u_Size.width) / scale_x;
        calc_height = (0x10000 * UnicamBase->u_Size.height) / scale_y;

        offset_x = (UnicamBase->u_DisplaySize.width - calc_width) >> 1;
        offset_y = (UnicamBase->u_DisplaySize.height - calc_height) >> 1;
    }

    ULONG startAddress = (ULONG)UnicamBase->u_ReceiveBuffer;
    startAddress += UnicamBase->u_Offset.x * (UnicamBase->u_BPP / 8);
    startAddress += UnicamBase->u_Offset.y * UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8);

    if (unity)
    {
        /* Unity scaling is simple, reserve less space for display list */
        cnt -= 9;

        UnicamBase->u_UnicamDL = cnt;

        /* Set control reg */
        displist[cnt++] = LE32(
            VC6_CONTROL_VALID
            | VC6_CONTROL_WORDS(8)
            | VC6_CONTROL_UNITY
            | VC6_CONTROL_ALPHA_EXPAND
            | VC6_CONTROL_RGB_EXPAND
            | VC6_CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB)
        );

        if (UnicamBase->u_BPP == 16)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565));
        else if (UnicamBase->u_BPP == 24)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888));

        /* Center it on the screen */
        displist[cnt++] = LE32(VC6_POS0_X(offset_x) | VC6_POS0_Y(offset_y));
        displist[cnt++] = LE32((VC6_SCALER_POS2_ALPHA_MODE_FIXED << VC6_SCALER_POS2_ALPHA_MODE_SHIFT) | VC6_SCALER_POS2_ALPHA(0xfff));
        displist[cnt++] = LE32(VC6_POS2_H(UnicamBase->u_Size.height) | VC6_POS2_W(UnicamBase->u_Size.width));
        displist[cnt++] = LE32(0xdeadbeef);

        /* Set address */
        displist[cnt++] = LE32(0xc0000000 | startAddress);
        displist[cnt++] = LE32(0xdeadbeef);

        /* Pitch is full width, always */
        displist[cnt++] = LE32(UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8));

        /* Done */
        displist[cnt++] = LE32(0x80000000);
    }
    else
    {
        cnt -= 18;
        
        UnicamBase->u_UnicamDL = cnt;

        /* Set control reg */
        displist[cnt++] = LE32(
            VC6_CONTROL_VALID
            | VC6_CONTROL_WORDS(17)
            | VC6_CONTROL_ALPHA_EXPAND
            | VC6_CONTROL_RGB_EXPAND
            | VC6_CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_XRGB)
        );

        if (UnicamBase->u_BPP == 16)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB565));
        else if (UnicamBase->u_BPP == 24)
            displist[cnt - 1] |= LE32(CONTROL_FORMAT(HVS_PIXEL_FORMAT_RGB888));

        /* Center plane on the screen */
        displist[cnt++] = LE32(VC6_POS0_X(offset_x) | VC6_POS0_Y(offset_y));
        displist[cnt++] = LE32((VC6_SCALER_POS2_ALPHA_MODE_FIXED << VC6_SCALER_POS2_ALPHA_MODE_SHIFT) | VC6_SCALER_POS2_ALPHA(0xfff));
        displist[cnt++] = LE32(VC6_POS1_H(calc_height) | VC6_POS1_W(calc_width));
        displist[cnt++] = LE32(VC6_POS2_H(UnicamBase->u_Size.height) | VC6_POS2_W(UnicamBase->u_Size.width));
        displist[cnt++] = LE32(0xdeadbeef); // Scratch written by HVS

        /* Set address and pitch */
        displist[cnt++] = LE32(0xc0000000 | startAddress);
        displist[cnt++] = LE32(0xdeadbeef);

        /* Pitch is full width, always */
        displist[cnt++] = LE32(UnicamBase->u_FullSize.width * (UnicamBase->u_BPP / 8));

        /* LMB address */
        displist[cnt++] = LE32(0);

        /* Set PPF Scaler */
        displist[cnt++] = LE32((scale_x << 8) | ((ULONG)UnicamBase->u_Scaler << 30) | UnicamBase->u_Phase);
        displist[cnt++] = LE32((scale_y << 8) | ((ULONG)UnicamBase->u_Scaler << 30) | UnicamBase->u_Phase);
        displist[cnt++] = LE32(0); // Scratch written by HVS

        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);
        displist[cnt++] = LE32(kernel);

        /* Done */
        displist[cnt++] = LE32(0x80000000);
    }
}
