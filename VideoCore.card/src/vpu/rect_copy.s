#
#   Copyright © 2021 Michal Schulz <michal.schulz@gmx.de>
#   https://github.com/michalsc
#
#   This Source Code Form is subject to the terms of the
#   Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
#   with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
#


# Copy block of memory using VPU - This function does not support overlap!
# Arguments:
#   r0 : start address of source
#   r1 : start address of destination
#   r2 : width in bytes
#   r3 : height in pixels
#   r4 : stride source - width in bytes
#   r5 : stride destination - width in bytes

        st r6, --(sp)
        st r7, --(sp)
        st r8, --(sp)
        mov r6, 64
        cmp r2, 0
        beq exit

# Repeat the rect copy loop as long as the height is not zero
loop:
        cmp r3, 0
        beq exit
        mov r7, r2

# Copy whole line now, do in largest possible blocks
        cmp r7, 4096
        bcs smaller_than_4K
loop_4K:
        vld HY(0++, 0), (r0+=r6) REP 64
        add r0, 4096
        sub r7, 4096
        cmp r7, 4096
        vst HY(0++, 0), (r1+=r6) REP 64
        add r1, 4096
        bcc loop_4K

smaller_than_4K:
        cmp r7, 1024
        bcs smaller_than_1K

loop_1K:
        vld HY(0++, 0), (r0+=r6) REP 16
        add r0, 1024
        sub r7, 1024
        cmp r7, 1024
        vst HY(0++, 0), (r1+=r6) REP 16
        add r1, 1024
        bcc loop_1K

smaller_than_1K:
        cmp r7, 256
        bcs smaller_than_256

loop_256:
        vld HY(0++, 0), (r0+=r6) REP 4
        add r0, 256
        sub r7, 256
        cmp r7, 256
        vst HY(0++, 0), (r1+=r6) REP 4
        add r1, 256
        bcc loop_256

smaller_than_256:
        cmp r7, 64
        bcs smaller_than_64

loop_64:  
        vld HY(0, 0), (r0)
        add r0, 64
        sub r7, 64
        cmp r7, 64
        vst HY(0, 0), (r1)
        add r1, 64
        bcc loop_64

smaller_than_64:

        cmp r7, 4
        bcs smaller_than_4

loop_4:
        ld r8, (r0)++
        st r8, (r1)++
        sub r7, 4
        cmp r7, 4
        bcc loop_4

smaller_than_4:
        cmp r7, 0
        beq copy_end
        ldb r8, (r0)++
        stb r8, (r1)++
        sub r7, 1
        b smaller_than_4

copy_end:
# Line copied, advance source and destination pointers
        add r0, r4
        add r1, r5
        sub r3, 1
        b   loop

exit:   
        ld r8, (sp)++
        ld r7, (sp)++
        ld r6, (sp)++        
        rts
