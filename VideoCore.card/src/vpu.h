#ifndef _VPU_H
#define _VPU_H

#include <exec/types.h>
#include "emu68-vc4.h"

int VPU_BlockCopy(APTR source, APTR dest, ULONG length, struct VC4Base *VC4Base);
int VPU_RectCopy(APTR source, APTR dest, ULONG width, ULONG height, ULONG stride_src, ULONG stride_dst, struct VC4Base *VC4Base);

#endif /* _VPU_H */
