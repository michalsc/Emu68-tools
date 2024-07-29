#ifndef _UNICAM_H
#define _UNICAM_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <common/compiler.h>
#include <stdint.h>
#include <resources/unicam.h>

#define UNICAM_VERSION  0
#define UNICAM_REVISION 2
#define UNICAM_PRIORITY 118

#define UNICAM_MODE     0x22
#define UNICAM_WIDTH    720
#define UNICAM_HEIGHT   576
#define UNICAM_BPP      16

struct Size {
    UWORD width;
    UWORD height;
};

struct Point {
    UWORD x;
    UWORD y;
};

struct UnicamBase {
    struct Library      u_Node;
    struct ExecBase *   u_SysBase;
    APTR                u_ROMBase;
    struct ConfigDev *  u_ConfigDev;
    APTR                u_MailBox;
    APTR                u_PeriphBase;
    ULONG *             u_Request;
    APTR                u_RequestBase;
    APTR                u_ReceiveBuffer;
    ULONG               u_ReceiveBufferSize;
    struct Size         u_DisplaySize;
    struct Size         u_Size;
    struct Point        u_Offset;
    struct Size         u_FullSize;
    ULONG               u_UnicamDL;
    ULONG               u_UnicamKernel;

    UWORD               u_KernelB;
    UWORD               u_KernelC;
    UWORD               u_Aspect;
    UBYTE               u_Scaler;
    UBYTE               u_Phase;
    UBYTE               u_Integer;
    UBYTE               u_Smooth;
    UBYTE               u_Mode;
    UBYTE               u_BPP;
    BOOL                u_StartOnBoot;
    BOOL                u_IsVC6;
};

#define UNICAM_FUNC_COUNT   10
#define BASE_NEG_SIZE       ((UNICAM_FUNC_COUNT) * 6)
#define BASE_POS_SIZE       (sizeof(struct UnicamBase))

static inline uint64_t LE64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint32_t LE32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t LE16(uint16_t x) { return __builtin_bswap16(x); }

void kprintf(REGARG(const char * msg, "a0"), REGARG(void * args, "a1"));

#define bug(string, ...) \
    do { ULONG args[] = {0, __VA_ARGS__}; kprintf(string, &args[1]); } while(0)

void unicam_run(ULONG *address , UBYTE lanes, UBYTE datatype, ULONG width , ULONG height , UBYTE bbp, struct UnicamBase * UnicamBase);
void unicam_stop(struct UnicamBase * UnicamBase);
void setup_csiclk(struct UnicamBase * UnicamBase);

void L_UnicamStart(REGARG(ULONG *address, "a0"), REGARG(UBYTE lanes, "d0"), REGARG(UBYTE datatype, "d1"),
                 REGARG(ULONG width, "d2"), REGARG(ULONG height, "d3"), REGARG(UBYTE bpp, "d4"),
                 REGARG(struct UnicamBase * UnicamBase, "a6"));
void L_UnicamStop(REGARG(struct UnicamBase * UnicamBase, "a6"));
APTR L_UnicamGetFramebuffer(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetFramebufferSize(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetCropSize(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetCropOffset(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetKernel(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetConfig(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetSize(REGARG(struct UnicamBase * UnicamBase, "a6"));
ULONG L_UnicamGetMode(REGARG(struct UnicamBase * UnicamBase, "a6"));

#endif /* _UNICAM_H */
