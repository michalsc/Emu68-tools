#include "emu68-vc4.h"
#include "vpu/block_copy.h"
#include "vpu/rect_copy.h"
#include "vpu/rect_copy_rev.h"
#include "mbox.h"

int VPU_BlockCopy(APTR source, APTR dest, ULONG length, struct VC4Base *VC4Base)
{
    bug("[VPU] BlockCopy(%08lx, %08lx, %ld)\n", source, dest, length);

    if (VC4Base->vc4_VPUCopyBlock == 0)
    {
        bug("[VPU]   Uploading function BlockCopy\n");
        VC4Base->vc4_VPUCopyBlock = upload_code(block_copy, sizeof(block_copy), VC4Base);
        bug("[VPU]   VPU Phys Base %08lx\n", VC4Base->vc4_VPUCopyBlock);
    }

    if (VC4Base->vc4_VPUCopyBlock == 0)
    {
        return 0;
    }

    call_code(VC4Base->vc4_VPUCopyBlock, (ULONG)source + 0xc0000000, (ULONG)dest + 0xc0000000, length, 0, 0, 0, VC4Base);

    return 1;
}

int VPU_RectCopy(APTR source, APTR dest, ULONG width, ULONG height, ULONG stride_src, ULONG stride_dst, struct VC4Base *VC4Base)
{
    if (source > dest)
    {
        if (VC4Base->vc4_VPUCopyRect == 0)
        {
            bug("[VPU]   Uploading function RectCopy\n");
            VC4Base->vc4_VPUCopyRect = upload_code(rect_copy, sizeof(rect_copy), VC4Base);
        }

        if (VC4Base->vc4_VPUCopyRect == 0)
        {
            return 0;
        }

        call_code(VC4Base->vc4_VPUCopyRect, (ULONG)source, (ULONG)dest, width, height, stride_src, stride_dst, VC4Base);
        
        /* Set lowest bit to prevent cache flushing next time */
        VC4Base->vc4_VPUCopyRect |= 1;
    }
    else
    {
        if (VC4Base->vc4_VPUCopyRectRev == 0)
        {
            bug("[VPU]   Uploading function RectCopyRev\n");
            VC4Base->vc4_VPUCopyRectRev = upload_code(rect_copy_rev, sizeof(rect_copy_rev), VC4Base);
        }

        if (VC4Base->vc4_VPUCopyRectRev == 0)
        {
            return 0;
        }

        source = (APTR)((ULONG)source + (height - 1) * (width + stride_src) + width);
        dest = (APTR)((ULONG)dest + (height - 1) * (width + stride_dst) + width);

        call_code(VC4Base->vc4_VPUCopyRectRev, (ULONG)source, (ULONG)dest, width, height, stride_src, stride_dst, VC4Base);
        
        /* Set lowest bit to prevent cache flushing next time */
        VC4Base->vc4_VPUCopyRect |= 1;
    }

    return 1;

}
