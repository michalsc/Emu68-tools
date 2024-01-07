/* Automatically generated header (sfdc 1.11f)! Do not edit! */

#ifndef CLIB_UNICAM_PROTOS_H
#define CLIB_UNICAM_PROTOS_H

/*
**   $VER: unicam_protos.h 0.1.0 $Id: unicam_lib.sfd 0.1.0 $
**
**   C prototypes. For use with 32 bit integers only.
**
**   Copyright (c) 2001 Amiga, Inc.
**       All Rights Reserved
*/

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* "unicam.resource" */
void UnicamStart(ULONG * address , UBYTE lanes, UBYTE datatype, ULONG width , ULONG height , UBYTE bbp);
void UnicamStop(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CLIB_UNICAM_PROTOS_H */