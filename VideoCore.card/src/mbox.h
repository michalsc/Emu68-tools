#ifndef _MBOX_H
#define _MBOX_H

#include "emu68-vc4.h"
#include <stdint.h>

void get_vc_memory(void **base, uint32_t *size, struct VC4Base * VC4Base);
struct Size get_display_size(struct VC4Base * VC4Base);
void init_display(struct Size dimensions, uint8_t depth, void **framebuffer, uint32_t *pitch, struct VC4Base * VC4Base);
int blank_screen(int blank, struct VC4Base *VC4Base);
uint32_t upload_code(const void * code, uint32_t code_size, struct VC4Base *VC4Base);
void call_code(uint32_t addr, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,
               uint32_t arg4, uint32_t arg5, struct VC4Base *VC4Base);
void release_framebuffer(struct VC4Base *VC4Base);

#endif /* _MBOX_H */
