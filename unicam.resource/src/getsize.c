/*
    Copyright © 2024 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <common/compiler.h>

#include "unicam.h"
#include "mbox.h"

ULONG L_UnicamGetSize(REGARG(struct UnicamBase * UnicamBase, "a6"))
{
    ULONG size = UnicamBase->u_FullSize.width;
    size = (size << 16) | UnicamBase->u_FullSize.height;
    return size;
}

ULONG L_UnicamGetMode(REGARG(struct UnicamBase * UnicamBase, "a6"))
{
    ULONG mode = UnicamBase->u_Aspect;
    mode = (mode << 8) | UnicamBase->u_Mode;
    mode = (mode << 8) | UnicamBase->u_BPP;
    return mode;
}
