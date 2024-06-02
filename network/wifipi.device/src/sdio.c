#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/execbase.h>
#if defined(__INTELLISENSE__)
#include <clib/exec_protos.h>
#else
#include <proto/exec.h>
#endif

#include "wifipi.h"
#include "sdio.h"
#include "brcm.h"
#include "brcm_sdio.h"
#include "brcm_chipcommon.h"

#define D(x) x

#define TIMEOUT_WAIT(check_func, tout) \
    do { ULONG cnt = (tout) / 10; if (cnt == 0) cnt = 1; while(cnt != 0) { if (check_func) break; \
    cnt = cnt - 1; delay_us(10, sdio->s_WiFiBase); }  } while(0)

void delay_us(ULONG us, struct WiFiBase *WiFiBase)
{
    (void)WiFiBase;
    ULONG timer = LE32(*(volatile ULONG*)0xf2003004);
    ULONG end = timer + us;

    if (end < timer) {
        while (end < LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
    }
    while (end > LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
}

// Set the clock dividers to generate a target value
ULONG get_clock_divider(ULONG base_clock, ULONG target_rate)
{
    ULONG targetted_divisor = 0;
    if(target_rate > base_clock)
    {
        targetted_divisor = 1;
    }
    else
    {
        targetted_divisor = base_clock / target_rate;
        ULONG mod = base_clock % target_rate;
        if(mod) {
            targetted_divisor--;
        }
    }

    // Decide on the clock mode to use

    // Currently only 10-bit divided clock mode is supported

    // HCI version 3 or greater supports 10-bit divided clock mode
    // This requires a power-of-two divider

    // Find the first bit set
    int divisor = -1;
    for(int first_bit = 31; first_bit >= 0; first_bit--)
    {
        ULONG bit_test = (1 << first_bit);
        if(targetted_divisor & bit_test)
        {
            divisor = first_bit;
            targetted_divisor &= ~bit_test;
            if(targetted_divisor)
            {
                // The divisor is not a power-of-two, increase it
                divisor++;
            }
            break;
        }
    }

    if(divisor == -1)
        divisor = 31;
    if(divisor >= 32)
        divisor = 31;

    if(divisor != 0)
        divisor = (1 << (divisor - 1));

    if(divisor >= 0x400)
        divisor = 0x3ff;

    ULONG freq_select = divisor & 0xff;
    ULONG upper_bits = (divisor >> 8) & 0x3;
    ULONG ret = (freq_select << 8) | (upper_bits << 6) | (0 << 5);

    return ret;
}

// Switch the clock rate whilst running
int switch_clock_rate(ULONG base_clock, ULONG target_rate, struct WiFiBase *WiFiBase)
{
    // Decide on an appropriate divider
    ULONG divider = get_clock_divider(base_clock, target_rate);

    // Wait for the command inhibit (CMD and DAT) bits to clear
    while(rd32(WiFiBase->w_SDIOBase, EMMC_STATUS) & 0x3)
        delay_us(1000, WiFiBase);

    // Set the SD clock off
    ULONG control1 = rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1);
    control1 &= ~(1 << 2);
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    delay_us(2000, WiFiBase);

    // Write the new divider
    control1 &= ~0xffe0;		// Clear old setting + clock generator select
    control1 |= divider;
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    delay_us(2000, WiFiBase);

    // Enable the SD clock
    control1 |= (1 << 2);
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    delay_us(2000, WiFiBase);

    return 0;
}

void cmd_int(ULONG cmd, ULONG arg, ULONG timeout, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    sdio->s_LastCMDSuccess = 0;

    // Check Command Inhibit
    while(rd32(sdio->s_SDIO, EMMC_STATUS) & 0x1)
        delay_us(10, sdio->s_WiFiBase);

    // Is the command with busy?
    if((cmd & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B)
    {
        // With busy

        // Is is an abort command?
        if((cmd & SD_CMD_TYPE_MASK) != SD_CMD_TYPE_ABORT)
        {
            // Not an abort command

            // Wait for the data line to be free
            while(rd32(sdio->s_SDIO, EMMC_STATUS) & 0x2)
                delay_us(10, sdio->s_WiFiBase);
        }
    }

    ULONG blksizecnt = sdio->s_BlockSize | (sdio->s_BlocksToTransfer << 16);

    wr32(sdio->s_SDIO, EMMC_BLKSIZECNT, blksizecnt);

    // Set argument 1 reg
    wr32(sdio->s_SDIO, EMMC_ARG1, arg);

    // Set command reg
    wr32(sdio->s_SDIO, EMMC_CMDTM, cmd);

    asm volatile("nop");
    //SDCardBase->sd_Delay(10, SDCardBase);

    // Wait for command complete interrupt
    TIMEOUT_WAIT((rd32(sdio->s_SDIO, EMMC_INTERRUPT) & 0x8001), timeout);
    ULONG irpts = rd32(sdio->s_SDIO, EMMC_INTERRUPT);

    // Clear command complete status
    wr32(sdio->s_SDIO, EMMC_INTERRUPT, 0xffff0001);

    // Test for errors
    if((irpts & 0xffff0001) != 0x1)
    {
        D(bug("[WiFI] error occured whilst waiting for command complete interrupt (%08lx), status: %08lx\n", irpts, rd32(sdio->s_SDIO, EMMC_STATUS)));
        D(bug("[WiFi]   CMD: %08lx, ARG: %08lx, BlocksToTransfer: %ld, BlockSize: %ld, Buffer: %08lx\n", cmd, arg, sdio->s_BlocksToTransfer, sdio->s_BlockSize, (ULONG)sdio->s_Buffer));
        D(bug("[WiFi]   last good CMD: %08lx, ARG: %08lx, BlocksToTransfer: %ld, BlockSize: %ld\n", sdio->lastCmd, sdio->lastArg, sdio->lastBlockCount, sdio->lastBlockSize));

        sdio->s_LastError = irpts & 0xffff0000;
        sdio->s_LastInterrupt = irpts;
        return;
    }

    // SDCardBase->sd_Delay(10, SDCardBase);
    asm volatile("nop");

    // Get response data
    switch(cmd & SD_CMD_RSPNS_TYPE_MASK)
    {
        case SD_CMD_RSPNS_TYPE_48:
        case SD_CMD_RSPNS_TYPE_48B:
            sdio->s_Res0 = rd32(sdio->s_SDIO, EMMC_RESP0);
            break;

        case SD_CMD_RSPNS_TYPE_136:
            sdio->s_Res0 = rd32(sdio->s_SDIO, EMMC_RESP0);
            sdio->s_Res1 = rd32(sdio->s_SDIO, EMMC_RESP1);
            sdio->s_Res2 = rd32(sdio->s_SDIO, EMMC_RESP2);
            sdio->s_Res3 = rd32(sdio->s_SDIO, EMMC_RESP3);
            break;
    }

    // If with data, wait for the appropriate interrupt
    if(cmd & SD_CMD_ISDATA)
    {
        ULONG wr_irpt;
        int is_write = 0;
        if(cmd & SD_CMD_DAT_DIR_CH)
            wr_irpt = (1 << 5);     // read
        else
        {
            is_write = 1;
            wr_irpt = (1 << 4);     // write
        }

        ULONG cur_block = 0;
        ULONG *cur_buf_addr = (ULONG *)sdio->s_Buffer;

        while(cur_block < sdio->s_BlocksToTransfer)
        {
            TIMEOUT_WAIT((rd32(sdio->s_SDIO, EMMC_INTERRUPT) & (wr_irpt | 0x8000)), timeout);
            irpts = rd32(sdio->s_SDIO, EMMC_INTERRUPT);
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, 0xffff0000 | wr_irpt);

            if((irpts & (0xffff0000 | wr_irpt)) != wr_irpt)
            {
                D(bug("[WiFi] error occured whilst waiting for data ready interrupt (%08lx)\n", irpts));
                D(bug("[WiFi]   CMD: %08lx, ARG: %08lx, Current block: %ld, BlocksToTransfer: %ld, BlockSize: %ld, Buffer: %08lx\n", cmd, arg, cur_block, sdio->s_BlocksToTransfer, sdio->s_BlockSize, (ULONG)sdio->s_Buffer));
                D(bug("[WiFi]   last good CMD: %08lx, ARG: %08lx, BlocksToTransfer: %ld, BlockSize: %ld\n", sdio->lastCmd, sdio->lastArg, sdio->lastBlockCount, sdio->lastBlockSize));

                sdio->s_LastError = irpts & 0xffff0000;
                sdio->s_LastInterrupt = irpts;
                return;
            }

            // Transfer the block
            if (is_write)
            {
                const ULONG word_count = sdio->s_BlockSize / 4;
                ULONG cnt = (word_count + 7) / 8;
                switch (word_count % 8) {
                    case 0: do {    *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 7:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 6:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 5:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 4:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 3:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 2:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    case 1:         *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA) = *cur_buf_addr++;
                    } while (--cnt > 0);
                }
            }
            else
            {
                const ULONG word_count = sdio->s_BlockSize / 4;
                ULONG cnt = (word_count + 7) / 8;
                switch (word_count % 8) {
                    case 0: do {    *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 7:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 6:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 5:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 4:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 3:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 2:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    case 1:         *cur_buf_addr++ = *(volatile ULONG *)((ULONG)sdio->s_SDIO + EMMC_DATA);
                    } while (--cnt > 0);
                }
            }
            cur_block++;
        }
    }
    // Wait for transfer complete (set if read/write transfer or with busy)
    if((((cmd & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B) ||
       (cmd & SD_CMD_ISDATA)))
    {
        // First check command inhibit (DAT) is not already 0
        if((rd32(sdio->s_SDIO, EMMC_STATUS) & 0x2) == 0)
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, 0xffff0002);
        else
        {
            TIMEOUT_WAIT((rd32(sdio->s_SDIO, EMMC_INTERRUPT) & 0x8002), timeout);
            irpts = rd32(sdio->s_SDIO, EMMC_INTERRUPT);
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, 0xffff0002);

            // Handle the case where both data timeout and transfer complete
            //  are set - transfer complete overrides data timeout: HCSS 2.2.17
            if(((irpts & 0xffff0002) != 0x2) && ((irpts & 0xffff0002) != 0x100002))
            {
                D(bug("[WiFi] error occured whilst waiting for transfer complete interrupt (%08lx)\n", irpts));
                D(bug("[WiFi]   CMD: %08lx, ARG: %08lx, BlocksToTransfer: %ld, BlockSize: %ld, Buffer: %08lx\n", cmd, arg, sdio->s_BlocksToTransfer, sdio->s_BlockSize, (ULONG)sdio->s_Buffer));
                D(bug("[WiFi]   last good CMD: %08lx, ARG: %08lx, BlocksToTransfer: %ld, BlockSize: %ld\n", sdio->lastCmd, sdio->lastArg, sdio->lastBlockCount, sdio->lastBlockSize));
                
                sdio->s_LastError = irpts & 0xffff0000;
                sdio->s_LastInterrupt = irpts;
                return;
            }
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, 0xffff0002);
        }
    }
    sdio->s_LastCMDSuccess = 1;
    sdio->lastArg = arg;
    sdio->lastCmd = cmd;
    sdio->lastBlockSize = sdio->s_BlockSize;
    sdio->lastBlockCount = sdio->s_BlocksToTransfer;
}

// Reset the CMD line
static int reset_cmd(struct SDIO *sdio)
{
    int tout = 1000000;
    ULONG control1 = rd32(sdio->s_SDIO, EMMC_CONTROL1);
    control1 |= SD_RESET_CMD;
    wr32(sdio->s_SDIO, EMMC_CONTROL1, control1);
    while (tout && (rd32(sdio->s_SDIO, EMMC_CONTROL1) & SD_RESET_CMD) != 0) {
        delay_us(1, sdio->s_WiFiBase);
        tout--;
    }
    if((rd32(sdio->s_SDIO, EMMC_CONTROL1) & SD_RESET_CMD) != 0)
    {
        return -1;
    }
    return 0;
}

// Reset the CMD line
static int reset_dat(struct SDIO *sdio)
{
    int tout = 1000000;
    ULONG control1 = rd32(sdio->s_SDIO, EMMC_CONTROL1);
    control1 |= SD_RESET_DAT;
    wr32(sdio->s_SDIO, EMMC_CONTROL1, control1);
    while (tout && (rd32(sdio->s_SDIO, EMMC_CONTROL1) & SD_RESET_DAT) != 0) {
        delay_us(1, sdio->s_WiFiBase);
        tout--;
    }
    if((rd32(sdio->s_SDIO, EMMC_CONTROL1) & SD_RESET_DAT) != 0)
    {
        return -1;
    }
    return 0;
}

static void handle_card_interrupt(struct SDIO *sdio)
{
    // Handle a card interrupt

    // Get the card status
    if(sdio->s_CardRCA)
    {
        cmd_int(SEND_STATUS, sdio->s_CardRCA << 16, 500000, sdio);
        if(FAIL(sdio))
        {
        }
        else
        {
        }
    }
    else
    {
    }
}

static void handle_interrupts(struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    ULONG irpts = rd32(sdio->s_SDIO, EMMC_INTERRUPT);
    ULONG reset_mask = 0;

    if(irpts & SD_COMMAND_COMPLETE)
    {
        D(bug("[WiFi] spurious command complete interrupt\n"));
        reset_mask |= SD_COMMAND_COMPLETE;
    }

    if(irpts & SD_TRANSFER_COMPLETE)
    {
        D(bug("[WiFi] spurious transfer complete interrupt\n"));
        reset_mask |= SD_TRANSFER_COMPLETE;
    }

    if(irpts & SD_BLOCK_GAP_EVENT)
    {
        D(bug("[WiFi] spurious block gap event interrupt\n"));
        reset_mask |= SD_BLOCK_GAP_EVENT;
    }

    if(irpts & SD_DMA_INTERRUPT)
    {
        D(bug("[WiFi] spurious DMA interrupt\n"));
        reset_mask |= SD_DMA_INTERRUPT;
    }

    if(irpts & SD_BUFFER_WRITE_READY)
    {
        D(bug("[WiFi] spurious buffer write ready interrupt\n"));
        reset_mask |= SD_BUFFER_WRITE_READY;
        reset_dat(sdio);
    }

    if(irpts & SD_BUFFER_READ_READY)
    {
        D(bug("[WiFi] spurious buffer read ready interrupt\n"));
        reset_mask |= SD_BUFFER_READ_READY;
        reset_dat(sdio);
    }

    if(irpts & SD_CARD_INSERTION)
    {
        D(bug("[WiFi] card insertion detected\n"));
        reset_mask |= SD_CARD_INSERTION;
    }

    if(irpts & SD_CARD_REMOVAL)
    {
        D(bug("[WiFi] card removal detected\n"));
        reset_mask |= SD_CARD_REMOVAL;
        //SDCardBase->sd_CardRemoval = 1;
    }

    if(irpts & SD_CARD_INTERRUPT)
    {
        handle_card_interrupt(sdio);
        reset_mask |= SD_CARD_INTERRUPT;
    }

    if(irpts & 0x8000)
    {
        reset_mask |= 0xffff0000;
    }

    wr32(sdio->s_SDIO, EMMC_INTERRUPT, reset_mask);
}

static void cmd(ULONG command, ULONG arg, ULONG timeout, struct SDIO *sdio)
{
    // First, handle any pending interrupts
    handle_interrupts(sdio);

    // Stop the command issue if it was the card remove interrupt that was
    //  handled
    if(0) //WiFIBase->w_CardRemoval)
    {
        sdio->s_LastCMDSuccess = 0;
        return;
    }

    // Now run the appropriate commands by calling sd_issue_command_int()
    if(command & IS_APP_CMD)
    {
        command &= 0x7fffffff;

        sdio->s_LastCMD = APP_CMD;

        ULONG rca = 0;
        if(sdio->s_CardRCA)
            rca = sdio->s_CardRCA << 16;

        cmd_int(APP_CMD, rca, timeout, sdio);
        if(sdio->s_LastCMDSuccess)
        {
            sdio->s_LastCMD = command | IS_APP_CMD;
            cmd_int(command, arg, timeout, sdio);
        }
    }
    else
    {
        sdio->s_LastCMD = command;
        cmd_int(command, arg, timeout, sdio);
    }
}

static UBYTE sdio_read_byte(UBYTE function, ULONG address, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    UBYTE res;

    S_LOCK(sdio);
    cmd(IO_RW_DIRECT, ((address & 0x1ffff) << 9) | ((function & 7) << 28), 500000, sdio);
    res = sdio->s_Res0;
    S_UNLOCK(sdio);

    return res;
}

static void sdio_write_byte(UBYTE function, ULONG address, UBYTE value, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    
    S_LOCK(sdio);
    cmd(IO_RW_DIRECT, value | 0x80000000 | ((address & 0x1ffff) << 9) | ((function & 7) << 28), 500000, sdio);
    S_UNLOCK(sdio);
}

static void sdio_write_bytes(UBYTE function, ULONG address, void *data, ULONG length, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    S_LOCK(sdio);
    sdio->s_Buffer = data;
    sdio->s_BlockSize = length;
    sdio->s_BlocksToTransfer = 1;
    cmd(IO_RW_EXTENDED | SD_DATA_WRITE, 0x80000000 | ((address & 0x1ffff) << 9) | ((function & 7) << 28) | (length & 0x1ff) | (1 << 26), 500000, sdio);
    S_UNLOCK(sdio);
}

static void sdio_read_bytes(UBYTE function, ULONG address, void *data, ULONG length, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    S_LOCK(sdio);
    sdio->s_Buffer = data;
    sdio->s_BlockSize = length;
    sdio->s_BlocksToTransfer = 1;
    cmd(IO_RW_EXTENDED | SD_DATA_READ, ((address & 0x1ffff) << 9) | ((function & 7) << 28) | (length & 0x1ff) | (1 << 26), 5000000, sdio);
    S_UNLOCK(sdio);
}

static void sdio_backplane_window(ULONG addr, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    /* Align address properly */
    addr = addr & ~SBSDIO_SB_OFT_ADDR_MASK;

    S_LOCK(sdio);
    if (addr != sdio->s_LastBackplaneWindow) {
        //D(bug("[WiFi] BackplaneWindow(%08lx)\n", addr));
        sdio->s_LastBackplaneWindow = addr;
        addr >>= 8;

        sdio_write_byte(SD_FUNC_BAK, SBSDIO_FUNC1_SBADDRLOW, addr, sdio);
        sdio_write_byte(SD_FUNC_BAK, SBSDIO_FUNC1_SBADDRMID, addr >> 8, sdio);
        sdio_write_byte(SD_FUNC_BAK, SBSDIO_FUNC1_SBADDRHIGH, addr >> 16, sdio);
    }
    S_UNLOCK(sdio);
}

static ULONG sdio_backplane_addr(ULONG addr, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    S_LOCK(sdio);
    sdio_backplane_window(addr, sdio);
    S_UNLOCK(sdio);

    return addr & SBSDIO_SB_OFT_ADDR_MASK;
}

static void sdio_bak_write32(ULONG address, ULONG data, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    data = LE32(data);
    
    S_LOCK(sdio);
    address = sdio_backplane_addr(address, sdio);
    sdio_write_bytes(SD_FUNC_BAK, address | SBSDIO_SB_ACCESS_2_4B_FLAG, &data, 4, sdio);
    S_UNLOCK(sdio);
}

static ULONG sdio_bak_read32(ULONG address, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    ULONG temp;

    S_LOCK(sdio);
    address = sdio_backplane_addr(address, sdio);
    sdio_read_bytes(SD_FUNC_BAK, address | SBSDIO_SB_ACCESS_2_4B_FLAG, &temp, 4, sdio);
    S_UNLOCK(sdio);

    return LE32(temp);
}

static int is_error(struct SDIO *sdio)
{
    return FAIL(sdio);
}

/* Turn backplane clock on or off */
static int brcmf_sdio_htclk(struct SDIO *sdio, UBYTE on, UBYTE pendingOK)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    
    UBYTE clkctl, clkreq, devctl;
    unsigned long timeout;

    clkctl = 0;

/*
    if (sdio->sr_enabled) {
        bus->clkstate = (on ? CLK_AVAIL : CLK_SDONLY);
        return 0;
    }
*/

    if (on)
    {
        /* Request HT Avail */
        clkreq = sdio->s_ALPOnly ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;

        sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, sdio);
        if (sdio->IsError(sdio)) {
            bug("[WiFi] HT Avail request error\n");
            return 0;
        }

        /* Check current status */
        clkctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
        
        /* Go to pending and await interrupt if appropriate */
        if (!SBSDIO_CLKAV(clkctl, sdio->s_ALPOnly) && pendingOK)
        {
            /* Allow only clock-available interrupt */
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            if (sdio->IsError(sdio)) {
                bug("[WiFi] Devctl error setting CA\n");
                return 0;
            }

            devctl |= SBSDIO_DEVCTL_CA_INT_ONLY;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            D(bug("[WiFi] CLKCTL: set PENDING\n"));
            sdio->s_ClkState = CLK_PENDING;

            return 0;
        }
        else if (sdio->s_ClkState == CLK_PENDING)
        {
            /* Cancel CA-only interrupt filter */
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
        }

        /* Otherwise, wait here (polling) for HT Avail */
        timeout = LE32(*(volatile ULONG*)0xf2003004) + PMU_MAX_TRANSITION_DLY * 10;
        while (!SBSDIO_CLKAV(clkctl, sdio->s_ALPOnly)) {
            clkctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
            if (timeout <= LE32(*(volatile ULONG*)0xf2003004))
                break;
            delay_us(10000, sdio->s_WiFiBase);
            bug("[WiFi] Waiting...\n");
        }

        if (sdio->IsError(sdio)) {
            bug("[WiFi] HT Avail request error\n");
            return 0;
        }
        if (!SBSDIO_CLKAV(clkctl, sdio->s_ALPOnly))
        {
            bug("[WiFi] HT Avail timeout (%ld): clkctl 0x%02lx\n",
                    PMU_MAX_TRANSITION_DLY, clkctl);
            return 0;
        }

        /* Mark clock available */
        sdio->s_ClkState = CLK_AVAIL;
        D(bug("[WiFi] CLKCTL: turned ON\n"));
    }
    else
    {
        clkreq = 0;

        if (sdio->s_ClkState == CLK_PENDING)
        {
            /* Cancel CA-only interrupt filter */
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl &= ~SBSDIO_DEVCTL_CA_INT_ONLY;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
        }

        sdio->s_ClkState = CLK_SDONLY;
        sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, clkreq, sdio);
        D(bug("[WiFi] CLKCTL: turned OFF\n"));
        if (sdio->IsError(sdio))
        {
            bug("[WiFi] Failed access turning clock off\n");
            return 0;
        }
    }
    return 1;
}

static int brcmf_sdio_sdclk(struct SDIO *sdio, UBYTE on)
{
    if (on)
       sdio->s_ClkState = CLK_SDONLY;
    else
       sdio->s_ClkState = CLK_NONE;

    return 1;
}

static int sdio_clkctrl(UBYTE target, UBYTE pendingOK, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    D(bug("[WiFi] SDIO ClkCTRL(%ld, %ld)\n", target, pendingOK));

    D(bug("[WiFi] sdio->s_ClkState = %ld\n", sdio->s_ClkState));

    if (sdio->s_ClkState == target)
    {
        return 1;
    }

    switch (target)
    {
        case CLK_AVAIL:
            /* Make sure SD clock is available */
            if (sdio->s_ClkState == CLK_NONE)
                brcmf_sdio_sdclk(sdio, TRUE);
            /* Now request HT Avail on the backplane */
            brcmf_sdio_htclk(sdio, TRUE, pendingOK);
            break;
        
        case CLK_SDONLY:
            /* Remove HT request, or bring up SD clock */
            if (sdio->s_ClkState == CLK_NONE)
                brcmf_sdio_sdclk(sdio, TRUE);
            else if (sdio->s_ClkState == CLK_AVAIL)
                brcmf_sdio_htclk(sdio, FALSE, FALSE);
            break;

        case CLK_NONE:
            /* Make sure to remove HT request */
            if (sdio->s_ClkState == CLK_AVAIL)
                brcmf_sdio_htclk(sdio, FALSE, FALSE);
            /* Now remove the SD clock */
            brcmf_sdio_sdclk(sdio, FALSE);
            break;
    }

    return 1;
}

void sdio_sendpkt(UBYTE *pkt, ULONG length, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    // Round up length to next 4 byte boundary
    length = (length + 3) & ~3;

    S_LOCK(sdio);

    ULONG block_count = length / 512;
    ULONG reminder = length % 512;

    if (block_count)
    {
        // Send out the data
        sdio->s_Buffer = pkt;
        sdio->s_BlockSize = 512;
        sdio->s_BlocksToTransfer = block_count;
        cmd(IO_RW_EXTENDED | SD_DATA_WRITE | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN, 0x80000000 |
            ((SD_FUNC_RAD & 7) << 28) | (1 << 27) | (block_count & 0x1ff) | (0 << 26), 5000000, sdio);
        pkt += block_count * 512;
    }

    if (reminder)
    {
        // Send out the data
        sdio->s_Buffer = pkt;
        sdio->s_BlockSize = reminder;
        sdio->s_BlocksToTransfer = 1;
        cmd(IO_RW_EXTENDED | SD_DATA_WRITE, 0x80000000 | ((SD_FUNC_RAD & 7) << 28) | (reminder & 0x1ff) | (0 << 26), 5000000, sdio);
    }

    S_UNLOCK(sdio);
}

void sdio_recvpkt(UBYTE *pkt, ULONG length, struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;

    // Round up length to next 4 byte boundary
    length = (length + 3) & ~3;

    S_LOCK(sdio);

    ULONG block_count = length / 512;
    ULONG reminder = length % 512;

    if (block_count)
    {
        // Send out the data
        sdio->s_Buffer = pkt;
        sdio->s_BlockSize = 512;
        sdio->s_BlocksToTransfer = block_count;
        cmd(IO_RW_EXTENDED | SD_DATA_READ | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN,
            ((SD_FUNC_RAD & 7) << 28) | (1 << 27) | (block_count & 0x1ff) | (0 << 26), 5000000, sdio);
        pkt += block_count * 512;
    }

    if (reminder)
    {
        // Send out the data
        sdio->s_Buffer = pkt;
        sdio->s_BlockSize = reminder;
        sdio->s_BlocksToTransfer = 1;
        cmd(IO_RW_EXTENDED | SD_DATA_READ, ((SD_FUNC_RAD & 7) << 28) | (reminder & 0x1ff) | (0 << 26), 5000000, sdio);
    }

    S_UNLOCK(sdio);
}

ULONG sdio_getintstatus(struct SDIO * sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    ULONG reg_addr;
    ULONG ints;

    S_LOCK(sdio);

    /* clear all interrupts */
    reg_addr = sdio->s_CC->c_BaseAddress + SD_REG(intstatus);

    D(bug("[WiFi] sdio_getintstatus, reg_addr = %08lx, ints = ", reg_addr));

    ints = sdio->Read32(reg_addr, sdio);
    D(bug("%08lx\n", ints));
    sdio->Write32(reg_addr, ints, sdio);

    S_UNLOCK(sdio);

    return ints;
}

struct SDIO *sdio_init(struct WiFiBase *WiFiBase)
{
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct SDIO *sdio = NULL;

    D(bug("[WiFi] SDIO init\n"));

    /* Allocate memory for SDIO structure */
    sdio = AllocPooledClear(WiFiBase->w_MemPool, sizeof(struct SDIO));
    if (sdio == NULL)
        return NULL;

    InitSemaphore(&sdio->s_Lock);

    /* Put few functions into the struct */
    sdio->IsError = is_error;
    sdio->BackplaneAddr = sdio_backplane_addr;
    sdio->WriteByte = sdio_write_byte;
    sdio->ReadByte = sdio_read_byte;
    sdio->Write = sdio_write_bytes;
    sdio->Read = sdio_read_bytes;
    sdio->Write32 = sdio_bak_write32;
    sdio->Read32 = sdio_bak_read32;
    sdio->ClkCTRL = sdio_clkctrl;

    sdio->SendPKT = sdio_sendpkt;
    sdio->RecvPKT = sdio_recvpkt;
    sdio->GetIntStatus = sdio_getintstatus;

    sdio->s_SDIO = WiFiBase->w_SDIOBase;
    sdio->s_WiFiBase = WiFiBase;
    sdio->s_SysBase = SysBase;

    // Make sure both buffers are enough to fit glom frames (32 times the normal frame size)
    sdio->s_TXBuffer = AllocPooled(WiFiBase->w_MemPool, 65536);
    sdio->s_RXBuffer = AllocPooled(WiFiBase->w_MemPool, 65536);

    ULONG ver = rd32(WiFiBase->w_SDIOBase, EMMC_SLOTISR_VER);
    ULONG vendor = ver >> 24;
    ULONG sdversion = (ver >> 16) & 0xff;
    ULONG slot_status = ver & 0xff;

    D(bug("[WiFi]   vendor %lx, sdversion %lx, slot_status %lx\n", vendor, sdversion, slot_status));

    ULONG control1 = rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1);
    control1 |= (1 << 24);
    // Disable clock
    control1 &= ~(1 << 2);
    control1 &= ~(1 << 0);
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1) & (0x7 << 24)) == 0, 1000000);
    if((rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1) & (7 << 24)) != 0)
    {
        D(bug("[WiFi]   controller did not reset properly\n"));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }

    D(bug("[WiFi]   control0: %08lx, control1: %08lx, control2: %08lx\n", 
            rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL0),
            rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1),
            rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL2)));

    TIMEOUT_WAIT(rd32(WiFiBase->w_SDIOBase, EMMC_STATUS) & (1 << 16), 500000);
    ULONG status_reg = rd32(WiFiBase->w_SDIOBase, EMMC_STATUS);
    if((status_reg & (1 << 16)) == 0)
    {
        D(bug("[WiFi]   no SDIO connected?\n"));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }

    D(bug("[WiFi]   status: %08lx\n", status_reg));

    // Clear control2
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL2, 0);

    control1 = rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1);
    control1 |= 1;      // enable clock

    // Set to identification frequency (400 kHz)
    uint32_t f_id = get_clock_divider(WiFiBase->w_SDIOClock, 400000);

    control1 |= f_id;

    control1 |= (7 << 16);		// data timeout = TMCLK * 2^10
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1) & 0x2), 1000000);
    if((rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1) & 0x2) == 0)
    {
        D(bug("[WiFI]   controller's clock did not stabilise within 1 second\n"));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }

    D(bug("[WiFi]   control0: %08lx, control1: %08lx\n",
        rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL0),
        rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1)));

    // Enable the SD clock
    delay_us(2000, WiFiBase);
    control1 = rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1);
    control1 |= 4;
    wr32(WiFiBase->w_SDIOBase, EMMC_CONTROL1, control1);
    delay_us(2000, WiFiBase);

    // Mask off sending interrupts to the ARM
    wr32(WiFiBase->w_SDIOBase, EMMC_IRPT_EN, 0);
    // Reset interrupts
    wr32(WiFiBase->w_SDIOBase, EMMC_INTERRUPT, 0xffffffff);
    
    // Have all interrupts sent to the INTERRUPT register
    uint32_t irpt_mask = 0xffffffff & (~SD_CARD_INTERRUPT);
#ifdef SD_CARD_INTERRUPTS
    irpt_mask |= SD_CARD_INTERRUPT;
#endif
    wr32(WiFiBase->w_SDIOBase, EMMC_IRPT_MASK, irpt_mask);

    delay_us(2000, WiFiBase);

    D(bug("[WiFi] Clock enabled, control0: %08lx, control1: %08lx\n",
        rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL0),
        rd32(WiFiBase->w_SDIOBase, EMMC_CONTROL1)));

    // Send CMD0 to the card (reset to idle state)
    cmd(GO_IDLE_STATE, 0, 500000, sdio);
    if(FAIL(sdio))
    {
        D(bug("[WiFi] SDIO: no CMD0 response\n"));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }

    // Send CMD8 to the card
    // Voltage supplied = 0x1 = 2.7-3.6V (standard)
    // Check pattern = 10101010b (as per PLSS 4.3.13) = 0xAA

    cmd(SEND_IF_COND, 0x1aa, 500000, sdio);

    if(CMD_TIMEOUT(sdio))
    {
        if(reset_cmd(sdio) == -1)
        {
            FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
            return NULL;
        }

        wr32(WiFiBase->w_SDIOBase, EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
    }
    else if(FAIL(sdio))
    {
        D(bug("[WiFi] failure sending CMD8 (%08lx)\n", sdio->s_LastInterrupt));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }
    else
    {
        if(((sdio->s_Res0) & 0xfff) != 0x1aa)
        {
            D(bug("[WiFi] unusable card\n"));
            D(bug("[WiFi] CMD8 response %08lx\n", sdio->s_Res0));
            FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
            return NULL;
        }
    }

    // Here we are supposed to check the response to CMD5 (HCSS 3.6)
    // It only returns if the card is a SDIO card
    cmd(IO_SET_OP_COND, 0, 10000, sdio);
    if(!TIMEOUT(sdio))
    {
        if(CMD_TIMEOUT(sdio))
        {
            if(reset_cmd(sdio) == -1)
            {
                FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
                return NULL;
            }
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
            D(bug("[WiFi] Not a SDIO card - aborting\n"));

            FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
            return NULL;
        }
        else
        {
            D(bug("[WiFi] SDIO card detected - CMD5 response %08lx\n", sdio->s_Res0));
        }
    }

    D(bug("[WiFi] Set host voltage to 3.3V\n"));
    cmd(IO_SET_OP_COND, 0x00200000, 10000, sdio);
    if(!TIMEOUT(sdio))
    {
        if(CMD_TIMEOUT(sdio))
        {
            if(reset_cmd(sdio) == -1)
            {
                FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
                return NULL;
            }
            wr32(sdio->s_SDIO, EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
        }
    }
    D(bug("[WiFi] CMD5 response %08lx\n", sdio->s_Res0));

    /* The card is SDIO. Increase speed to standard 25MHz and obtain CID as well as RCA */
    ULONG _clk = (SD_CLOCK_NORMAL + 50000) / 100000;
    D(bug("[WiFi] Switching clock to %ld.%ldMHz\n", _clk / 10, _clk % 10));
    switch_clock_rate(WiFiBase->w_SDIOClock, SD_CLOCK_NORMAL, WiFiBase);

    delay_us(10000, WiFiBase);

    // Send CMD3 to enter the data state
    cmd(SEND_RELATIVE_ADDR, 0, 500000, sdio);
    if(FAIL(sdio))
    {
        D(bug("[WiFi] error sending SEND_RELATIVE_ADDR\n"));
        FreePooled(WiFiBase->w_MemPool, sdio, sizeof(struct SDIO));
        return NULL;
    }

    sdio->s_CardRCA = sdio->s_Res0 >> 16;

    D(bug("[WiFi] SEND_RELATIVE_ADDR returns %08lx\n", sdio->s_Res0));

    cmd(SELECT_CARD, sdio->s_CardRCA << 16, 500000, sdio);

    D(bug("[WiFi] Card selected, return value %08lx\n", sdio->s_Res0));

    D(bug("[WiFi] Selecting 4bit mode\n"));
    UBYTE cccr7 = sdio_read_byte(SD_FUNC_CIA, BUS_BI_CTRL_REG, sdio);
    if (SUCCESS(sdio))
    {
        cccr7 |= 0x80;      // Disable card detect pullup
        cccr7 &= ~0x07;     // Clear width bits
        cccr7 |= 0x02;      // Select 4 bit interface
        sdio_write_byte(SD_FUNC_CIA, BUS_BI_CTRL_REG, cccr7, sdio);

        /* Set 4bit width in CONTROL0 register */
        wr32(sdio->s_SDIO, EMMC_CONTROL0, 2 | rd32(sdio->s_SDIO, EMMC_CONTROL0));
    }

    D(bug("[WiFi] SDIO @ %08lx\n", (ULONG)sdio));

    return sdio;
}
