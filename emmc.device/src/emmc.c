#include <exec/types.h>
#include <stdint.h>
#include "emmc.h"
#include "mbox.h"

int emmc_power_cycle(struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

    bug("[brcm-emmc] powerCycle\n");

    bug("[brcm-emmc]   power OFF\n");
    set_power_state(0, 2, EMMCBase);
    set_extgpio_state(6, 0, EMMCBase);

    delay(500000, EMMCBase);

    bug("[brcm-emmc]   power ON\n");
    set_extgpio_state(6, 1, EMMCBase);
    return set_power_state(0, 3, EMMCBase);
}

void led(int on, struct EMMCBase *EMMCBase)
{
    if (on) {
        wr32((APTR)0xf2200000, 0x20, 1 << 10);
    }
    else {
        wr32((APTR)0xf2200000, 0x2c, 1 << 10);
    }
}

void cmd_int(ULONG cmd, ULONG arg, ULONG timeout, struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

    ULONG tout = 0;
    EMMCBase->emmc_LastCMDSuccess = 0;

    // Check Command Inhibit
    while(rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & 0x1)
        delay(10, EMMCBase);

    // Is the command with busy?
    if((cmd & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B)
    {
        // With busy

        // Is is an abort command?
        if((cmd & SD_CMD_TYPE_MASK) != SD_CMD_TYPE_ABORT)
        {
            // Not an abort command

            // Wait for the data line to be free
            while(rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & 0x2)
                delay(10, EMMCBase);
        }
    }

    uint32_t blksizecnt = EMMCBase->emmc_BlockSize | (EMMCBase->emmc_BlocksToTransfer << 16);

    wr32(EMMCBase->emmc_Regs, EMMC_BLKSIZECNT, blksizecnt);

    // Set argument 1 reg
    wr32(EMMCBase->emmc_Regs, EMMC_ARG1, arg);

#if 0
    {
        ULONG args[] = {cmd, arg};
        RawDoFmt("[brcm-sdhc] sending command %08lx, arg %08lx\n", args, (APTR)putch, NULL);
    }
#endif

    // Set command reg
    wr32(EMMCBase->emmc_Regs, EMMC_CMDTM, cmd);

    asm volatile("nop");
    //SDCardBase->sd_Delay(10, SDCardBase);

    // Wait for command complete interrupt
    TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT) & 0x8001), timeout);
    uint32_t irpts = rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT);

    // Clear command complete status
    wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffff0001);

    // Test for errors
    if((irpts & 0xffff0001) != 0x1)
    {
        bug("[brcm-emmc] Error occured whilst waiting for command complete interrupt (%08lx)\n", irpts);

        EMMCBase->emmc_LastError = irpts & 0xffff0000;
        EMMCBase->emmc_LastInterrupt = irpts;
        return;
    }

//    SDCardBase->sd_Delay(10, SDCardBase);
    asm volatile("nop");

    // Get response data
    switch(cmd & SD_CMD_RSPNS_TYPE_MASK)
    {
        case SD_CMD_RSPNS_TYPE_48:
        case SD_CMD_RSPNS_TYPE_48B:
            EMMCBase->emmc_Res0 = rd32(EMMCBase->emmc_Regs, EMMC_RESP0);
            break;

        case SD_CMD_RSPNS_TYPE_136:
            EMMCBase->emmc_Res0 = rd32(EMMCBase->emmc_Regs, EMMC_RESP0);
            EMMCBase->emmc_Res1 = rd32(EMMCBase->emmc_Regs, EMMC_RESP1);
            EMMCBase->emmc_Res2 = rd32(EMMCBase->emmc_Regs, EMMC_RESP2);
            EMMCBase->emmc_Res3 = rd32(EMMCBase->emmc_Regs, EMMC_RESP3);
            break;
    }
    // If with data, wait for the appropriate interrupt
    if(cmd & SD_CMD_ISDATA)
    {
        uint32_t wr_irpt;
        int is_write = 0;
        if(cmd & SD_CMD_DAT_DIR_CH)
            wr_irpt = (1 << 5);     // read
        else
        {
            is_write = 1;
            wr_irpt = (1 << 4);     // write
        }

        int cur_block = 0;
        uint32_t *cur_buf_addr = (uint32_t *)EMMCBase->emmc_Buffer;
        while(cur_block < EMMCBase->emmc_BlocksToTransfer)
        {
            tout = timeout / 100;
            TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT) & (wr_irpt | 0x8000)), timeout);
            irpts = rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT);
            wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffff0000 | wr_irpt);

            if((irpts & (0xffff0000 | wr_irpt)) != wr_irpt)
            {
                bug("[brcm-emmc] Error occured whilst waiting for data ready interrupt (%08lx)\n", irpts);

                EMMCBase->emmc_LastError = irpts & 0xffff0000;
                EMMCBase->emmc_LastInterrupt = irpts;
                return;
            }

            // Transfer the block
            UWORD cur_byte_no = 0;
            while(cur_byte_no < EMMCBase->emmc_BlockSize)
            {
                if(is_write)
				{
					uint32_t data = *(ULONG*)cur_buf_addr;
                    wr32be(EMMCBase->emmc_Regs, EMMC_DATA, data);
				}
                else
				{
					uint32_t data = rd32be(EMMCBase->emmc_Regs, EMMC_DATA);
					*(ULONG*)cur_buf_addr = data;
				}
                cur_byte_no += 4;
                cur_buf_addr++;
            }

            cur_block++;
        }
    }
    // Wait for transfer complete (set if read/write transfer or with busy)
    if((((cmd & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B) ||
       (cmd & SD_CMD_ISDATA)))
    {
        // First check command inhibit (DAT) is not already 0
        if((rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & 0x2) == 0)
            wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffff0002);
        else
        {
            TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT) & 0x8002), timeout);
            irpts = rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT);
            wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffff0002);

            // Handle the case where both data timeout and transfer complete
            //  are set - transfer complete overrides data timeout: HCSS 2.2.17
            if(((irpts & 0xffff0002) != 0x2) && ((irpts & 0xffff0002) != 0x100002))
            {
                bug("[brcm-emmc] Error occured whilst waiting for transfer complete interrupt (%08lx)\n", irpts);
                EMMCBase->emmc_LastError = irpts & 0xffff0000;
                EMMCBase->emmc_LastInterrupt = irpts;
                return;
            }
            wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffff0002);
        }
    }
    EMMCBase->emmc_LastCMDSuccess = 1;
}

// Reset host
static int emmc_reset_host(struct EMMCBase *EMMCBase)
{
    int tout = 1000000;
    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
	control1 |= SD_RESET_ALL;
	wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
	while (tout && (rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_ALL) != 0) {
        delay(1, EMMCBase);
        tout--;
    }
	if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_ALL) != 0)
	{
        bug("[brcm-emmc] reset ALL timed out, Control1: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1));
		return -1;
	}
	return 0;
}

// Reset the CMD line
static int emmc_reset_cmd(struct EMMCBase *EMMCBase)
{
    int tout = 1000000;
    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
	control1 |= SD_RESET_CMD;
	wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
	while (tout && (rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_CMD) != 0) {
        delay(1, EMMCBase);
        tout--;
    }
	if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_CMD) != 0)
	{
        bug("[brcm-emmc] reset CMD timed out, Control1: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1));
		return -1;
	}
	return 0;
}

// Reset the CMD line
static int emmc_reset_dat(struct EMMCBase *EMMCBase)
{
    int tout = 1000000;
    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
	control1 |= SD_RESET_DAT;
	wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
	while (tout && (rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_DAT) != 0) {
        delay(1, EMMCBase);
        tout--;
    }
	if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & SD_RESET_DAT) != 0)
	{
        bug("[brcm-emmc] reset DAT timed out, Control1: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1));
		return -1;
	}
	return 0;
}

static void emmc_handle_card_interrupt(struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

    // Handle a card interrupt

    // Get the card status
    if(EMMCBase->emmc_CardRCA)
    {
        cmd_int(SEND_STATUS, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
        if(FAIL(EMMCBase))
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

static void emmc_handle_interrupts(struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;
    uint32_t irpts = rd32(EMMCBase->emmc_Regs, EMMC_INTERRUPT);
    uint32_t reset_mask = 0;

    if(irpts & SD_COMMAND_COMPLETE)
    {
        bug("[brcm-emmc] spurious command complete interrupt\n");
        reset_mask |= SD_COMMAND_COMPLETE;
    }

    if(irpts & SD_TRANSFER_COMPLETE)
    {
        bug("[brcm-emmc] spurious transfer complete interrupt\n");
        reset_mask |= SD_TRANSFER_COMPLETE;
    }

    if(irpts & SD_BLOCK_GAP_EVENT)
    {
        bug("[brcm-emmc] spurious block gap event interrupt\n");
        reset_mask |= SD_BLOCK_GAP_EVENT;
    }

    if(irpts & SD_DMA_INTERRUPT)
    {
        bug("[brcm-emmc] spurious DMA interrupt\n");
        reset_mask |= SD_DMA_INTERRUPT;
    }

    if(irpts & SD_BUFFER_WRITE_READY)
    {
        bug("[brcm-emmc] spurious buffer write ready interrupt\n");
        reset_mask |= SD_BUFFER_WRITE_READY;
        emmc_reset_dat(EMMCBase);
    }

    if(irpts & SD_BUFFER_READ_READY)
    {
        bug("[brcm-emmc] spurious buffer read ready interrupt\n");
        reset_mask |= SD_BUFFER_READ_READY;
        emmc_reset_dat(EMMCBase);
    }

    if(irpts & SD_CARD_INSERTION)
    {
        bug("[brcm-emmc] card insertion detected\n");
        reset_mask |= SD_CARD_INSERTION;
    }

    if(irpts & SD_CARD_REMOVAL)
    {
        bug("[brcm-emmc] card removal detected\n");
        reset_mask |= SD_CARD_REMOVAL;
        EMMCBase->emmc_CardRemoval = 1;
    }

    if(irpts & SD_CARD_INTERRUPT)
    {
        emmc_handle_card_interrupt(EMMCBase);
        reset_mask |= SD_CARD_INTERRUPT;
    }

    if(irpts & 0x8000)
    {
        reset_mask |= 0xffff0000;
    }

    wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, reset_mask);
}

void emmc_cmd(ULONG command, ULONG arg, ULONG timeout, struct EMMCBase *EMMCBase)
{
    // First, handle any pending interrupts
    emmc_handle_interrupts(EMMCBase);

    // Stop the command issue if it was the card remove interrupt that was
    //  handled
    if(EMMCBase->emmc_CardRemoval)
    {
        EMMCBase->emmc_LastCMDSuccess = 0;
        return;
    }

    // Now run the appropriate commands by calling sd_issue_command_int()
    if(command & IS_APP_CMD)
    {
        command &= 0x7fffffff;

        EMMCBase->emmc_LastCMD = APP_CMD;

        uint32_t rca = 0;
        if(EMMCBase->emmc_CardRCA)
            rca = EMMCBase->emmc_CardRCA << 16;
        cmd_int(APP_CMD, rca, timeout, EMMCBase);
        if(EMMCBase->emmc_LastCMDSuccess)
        {
            EMMCBase->emmc_LastCMD = command | IS_APP_CMD;
            cmd_int(command, arg, timeout, EMMCBase);
        }
    }
    else
    {
        EMMCBase->emmc_LastCMD = command;
        cmd_int(command, arg, timeout, EMMCBase);
    }
}


// Set the clock dividers to generate a target value
static uint32_t emmc_get_clock_divider(uint32_t base_clock, uint32_t target_rate)
{
    // TODO: implement use of preset value registers

    uint32_t targetted_divisor = 0;
    if(target_rate > base_clock)
        targetted_divisor = 1;
    else
    {
        targetted_divisor = base_clock / target_rate;
        uint32_t mod = base_clock % target_rate;
        if(mod)
            targetted_divisor--;
    }

    // Decide on the clock mode to use

    // Currently only 10-bit divided clock mode is supported

    // HCI version 3 or greater supports 10-bit divided clock mode
    // This requires a power-of-two divider

    // Find the first bit set
    int divisor = -1;
    for(int first_bit = 31; first_bit >= 0; first_bit--)
    {
        uint32_t bit_test = (1 << first_bit);
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

    uint32_t freq_select = divisor & 0xff;
    uint32_t upper_bits = (divisor >> 8) & 0x3;
    uint32_t ret = (freq_select << 8) | (upper_bits << 6) | (0 << 5);

    return ret;
}

// Switch the clock rate whilst running
static int emmc_switch_clock_rate(uint32_t base_clock, uint32_t target_rate, struct EMMCBase *EMMCBase)
{
    // Decide on an appropriate divider
    uint32_t divider = emmc_get_clock_divider(base_clock, target_rate);

    // Wait for the command inhibit (CMD and DAT) bits to clear
    while(rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & 0x3)
        delay(1000, EMMCBase);

    // Set the SD clock off
    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 &= ~(1 << 2);
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    delay(2000, EMMCBase);

    // Write the new divider
    control1 &= ~0xffe0;		// Clear old setting + clock generator select
    control1 |= divider;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    delay(2000, EMMCBase);

    // Enable the SD clock
    control1 |= (1 << 2);
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    delay(2000, EMMCBase);

    return 0;
}

int emmc_microsd_init(struct EMMCBase *EMMCBase)
{
    ULONG tout;
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

    bug("[brcm-emmc] Trying to initialise as SD device\n");

    emmc_power_cycle(EMMCBase);

    emmc_reset_host(EMMCBase);

    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= (1 << 24);
    // Disable clock
    control1 &= ~(1 << 2);
    control1 &= ~(1 << 0);
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & (0x7 << 24)) == 0, 1000000);
    if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & (7 << 24)) != 0)
    {
        bug("[brcm-emmc] Controller did not reset properly\n");
        return -1;
    }

    bug("[brcm-emmc] control0: %08lx, control1: %08lx, control2: %08lx\n",
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL2));
    
    EMMCBase->emmc_Capabilities0 = rd32(EMMCBase->emmc_Regs, EMMC_CAPABILITIES_0);
    EMMCBase->emmc_Capabilities1 = rd32(EMMCBase->emmc_Regs, EMMC_CAPABILITIES_1);

    bug("[brcm-emmc] Capabilities: %08lx:%08lx\n", EMMCBase->emmc_Capabilities1, EMMCBase->emmc_Capabilities0);

    TIMEOUT_WAIT(rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & (1 << 16), 500000);
    uint32_t status_reg = rd32(EMMCBase->emmc_Regs, EMMC_STATUS);
    if((status_reg & (1 << 16)) == 0)
    {
        bug("[brcm-emmc] No card inserted\n");
        return -1;
    }

    bug("[brcm-emmc] Status: %08lx\n", status_reg);

    // Clear control2
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL2, 0);

    // Set eMMC clock to 200MHz
    set_clock_rate(12, 250000000, EMMCBase);
    set_clock_rate(1, 250000000, EMMCBase);

    // Get the base clock rate
    uint32_t base_clock = get_clock_rate(12, EMMCBase);

    bug("[brcm-emmc] Base clock: %ld Hz\n", base_clock);

    control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= 1;			// enable clock

    // Set to identification frequency (400 kHz)
    uint32_t f_id = emmc_get_clock_divider(base_clock, SD_CLOCK_ID);

    control1 |= f_id;

    control1 |= (7 << 16);		// data timeout = TMCLK * 2^10
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & 0x2), 1000000);
    if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & 0x2) == 0)
    {
        bug("[brcm-emmc] EMMC: controller's clock did not stabilise within 1 second\n");
        return -1;
    }

    bug("[brcm-emmc] control0: %08lx, control1: %08lx, control2: %08lx\n",
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL2));

    // Enable the SD clock
    delay(2000, EMMCBase);
    control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= 4;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    delay(2000, EMMCBase);

    // Mask off sending interrupts to the ARM
    wr32(EMMCBase->emmc_Regs, EMMC_IRPT_EN, 0);
    // Reset interrupts
    wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffffffff);

    // Have all interrupts sent to the INTERRUPT register
    uint32_t irpt_mask = 0xffffffff & (~SD_CARD_INTERRUPT);
#ifdef SD_CARD_INTERRUPTS
    irpt_mask |= SD_CARD_INTERRUPT;
#endif
    wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, irpt_mask);

    delay(2000, EMMCBase);

    bug("[brcm-emmc] Clock enabled, control0: %08lx, control1: %08lx\n",
        rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0),
        rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1));

    // Enable 3.3V on the bus
    uint32_t control0 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0);
    control0 &= 0xffff00ff;
    control0 |= 0x00000e00;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);
    delay(2000, EMMCBase);
    control0 |= 0x00000100;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);
    delay(2000, EMMCBase);


    // Send CMD0 to the card (reset to idle state)
	emmc_cmd(GO_IDLE_STATE, 0, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
        bug("[brcm-emmc] No CMD0 response\n");
        bug("[brcm-emmc] Status: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_STATUS));
        return -1;
	}

    // Send CMD8 to the card
	// Voltage supplied = 0x1 = 2.7-3.6V (standard)
	// Check pattern = 10101010b (as per PLSS 4.3.13) = 0xAA

    bug("[brcm-emmc] note a timeout error on the following command (CMD8) is normal "
           "and expected if the SD card version is less than 2.0\n");

    emmc_cmd(SEND_IF_COND, 0x1aa, 500000, EMMCBase);

	int v2_later = 0;
	if(TIMEOUT(EMMCBase))
        v2_later = 0;
    else if(CMD_TIMEOUT(EMMCBase))
    {
        bug("[brcm-emmc] CMD8 timed out\n");
        if(emmc_reset_cmd(EMMCBase) == -1)
            return -1;
        wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
        v2_later = 0;
    }
    else if(FAIL(EMMCBase))
    {
        bug("[brcm-emmc] Failure sending CMD8 (%08lx)\n", EMMCBase->emmc_LastInterrupt);
        return -1;
    }
    else
    {
        if(((EMMCBase->emmc_Res0) & 0xfff) != 0x1aa)
        {
            bug("[brcm-emmc] Unusable card\n");
            bug("[brcm-emmc] CMD8 response %08lx\n", EMMCBase->emmc_Res0);
            return -1;
        }
        else
            v2_later = 1;
    }

    bug("[brcm-emmc] CMD8 response %08lx\n", EMMCBase->emmc_Res0);

    // Here we are supposed to check the response to CMD5 (HCSS 3.6)
    // It only returns if the card is a SDIO card
    bug("[brcm-emmc] Note that a timeout error on the following command (CMD5) is "
           "normal and expected if the card is not a SDIO card.\n");
    emmc_cmd(IO_SET_OP_COND, 0, 10000, EMMCBase);
    if(!TIMEOUT(EMMCBase))
    {
        if(CMD_TIMEOUT(EMMCBase))
        {
            if(emmc_reset_cmd(EMMCBase) == -1)
                return -1;
            wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
        }
        else
        {
            bug("[brcm-emmc] SDIO card detected - not currently supported\n");
            return -1;
        }
    }

    // Call initialization ACMD41
	int card_is_busy = 1;
	while(card_is_busy)
	{
	    uint32_t v2_flags = 0;
	    if(v2_later)
	    {
	        // Set SDHC support
	        v2_flags |= (1 << 30);
	    }

	    emmc_cmd(ACMD_41 | IS_APP_CMD, 0x00ff8000 | v2_flags, 500000, EMMCBase);

	    if(FAIL(EMMCBase))
	    {
	        bug("[brcm-emmc] Error issuing ACMD41\n");
	        return -1;
	    }

	    if((EMMCBase->emmc_Res0) & 0x80000000)
	    {
	        // Initialization is complete
	        EMMCBase->emmc_CardOCR = (EMMCBase->emmc_Res0 >> 8) & 0xffff;
	        EMMCBase->emmc_CardSupportsSDHC = (EMMCBase->emmc_Res0 >> 30) & 0x1;

	        card_is_busy = 0;
	    }
	    else
	    {
            //RawDoFmt("[brcm-sdhc] card is busy, retrying\n", NULL, (APTR)putch, NULL);
            delay(500000, EMMCBase);
	    }       
	}

    bug("[brcm-sdhc] card identified: OCR: %04lx, SDHC support: %ld\n", EMMCBase->emmc_CardOCR, EMMCBase->emmc_CardSupportsSDHC);


    // At this point, we know the card is definitely an SD card, so will definitely
	//  support SDR12 mode which runs at 25 MHz
    emmc_switch_clock_rate(base_clock, SD_CLOCK_NORMAL, EMMCBase);

	// A small wait before the voltage switch
	delay(5000, EMMCBase);

	// Send CMD2 to get the cards CID
	emmc_cmd(ALL_SEND_CID, 0, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending ALL_SEND_CID\n");
	    return -1;
	}
	uint32_t card_cid_0 = EMMCBase->emmc_Res0;
	uint32_t card_cid_1 = EMMCBase->emmc_Res1;
	uint32_t card_cid_2 = EMMCBase->emmc_Res2;
	uint32_t card_cid_3 = EMMCBase->emmc_Res3;

    bug("[brcm-emmc] card CID: %08lx%08lx%08lx%08lx\n", card_cid_3, card_cid_2, card_cid_1, card_cid_0);


    EMMCBase->emmc_CID[0] = card_cid_3;
    EMMCBase->emmc_CID[1] = card_cid_2;
    EMMCBase->emmc_CID[2] = card_cid_1;
    EMMCBase->emmc_CID[3] = card_cid_0;

	// Send CMD3 to enter the data state
	emmc_cmd(SEND_RELATIVE_ADDR, 0, 500000, EMMCBase);
	if(FAIL(EMMCBase))
    {
        bug("[brcm-emmc] Error sending SEND_RELATIVE_ADDR\n");
        return -1;
    }

    uint32_t cmd3_resp = EMMCBase->emmc_Res0;

	EMMCBase->emmc_CardRCA = (cmd3_resp >> 16) & 0xffff;
	uint32_t crc_error = (cmd3_resp >> 15) & 0x1;
	uint32_t illegal_cmd = (cmd3_resp >> 14) & 0x1;
	uint32_t error = (cmd3_resp >> 13) & 0x1;
	uint32_t status = (cmd3_resp >> 9) & 0xf;
	uint32_t ready = (cmd3_resp >> 8) & 0x1;

    bug("[brcm-emmc] Res0: %08lx\n", cmd3_resp);

	if(crc_error)
	{
		bug("[brcm-emmc] CRC error\n");
		return -1;
	}

	if(illegal_cmd)
	{
		bug("[brcm-emmc] Illegal command\n");
		return -1;
	}

	if(error)
	{
		bug("[brcm-emmc] generic error\n");
		return -1;
	}

	if(!ready)
	{
		bug("[brcm-emmc] not ready for data\n");
		return -1;
	}

    bug("[brcm-emmc] RCA: %04lx\n", EMMCBase->emmc_CardRCA);

	// Now select the card (toggles it to transfer state)
	emmc_cmd(SELECT_CARD, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending CMD7\n");
	    return -1;
	}

    uint32_t cmd7_resp = EMMCBase->emmc_Res0;
	status = (cmd7_resp >> 9) & 0xf;

	if((status != 3) && (status != 4))
	{
		bug("[brcm-emmc] invalid status (%ld)\n", status);
		return -1;
	}

	// If not an SDHC card, ensure BLOCKLEN is 512 bytes
	if(!EMMCBase->emmc_CardSupportsSDHC)
	{
	    emmc_cmd(SET_BLOCKLEN, 512, 500000, EMMCBase);
	    if(FAIL(EMMCBase))
	    {
	        bug("[brcm-emmc] Error sending SET_BLOCKLEN\n");
	        return -1;
	    }
	}

	uint32_t controller_block_size = rd32(EMMCBase->emmc_Regs, EMMC_BLKSIZECNT);
	controller_block_size &= (~0xfff);
	controller_block_size |= 0x200;
	wr32(EMMCBase->emmc_Regs, EMMC_BLKSIZECNT, controller_block_size);

    // Get the cards SCR register
    EMMCBase->emmc_Buffer = &EMMCBase->emmc_SCR;
    EMMCBase->emmc_BlocksToTransfer = 1;
    EMMCBase->emmc_BlockSize = 8;

    emmc_cmd(SEND_SCR, 0, 500000, EMMCBase);
	EMMCBase->emmc_BlockSize = 512;

	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending SEND_SCR\n");
	    return -1;
	}

	// Determine card version
	// Note that the SCR is big-endian
	uint32_t scr0 = EMMCBase->emmc_SCR.scr[0];
    EMMCBase->emmc_SCR.emmc_version = SD_VER_UNKNOWN;
	uint32_t sd_spec = (scr0 >> (56 - 32)) & 0xf;
	uint32_t sd_spec3 = (scr0 >> (47 - 32)) & 0x1;
	uint32_t sd_spec4 = (scr0 >> (42 - 32)) & 0x1;
    EMMCBase->emmc_SCR.emmc_bus_widths = (scr0 >> (48 - 32)) & 0xf;
	if(sd_spec == 0)
        EMMCBase->emmc_SCR.emmc_version = SD_VER_1;
    else if(sd_spec == 1)
        EMMCBase->emmc_SCR.emmc_version = SD_VER_1_1;
    else if(sd_spec == 2)
    {
        if(sd_spec3 == 0)
            EMMCBase->emmc_SCR.emmc_version = SD_VER_2;
        else if(sd_spec3 == 1)
        {
            if(sd_spec4 == 0)
                EMMCBase->emmc_SCR.emmc_version = SD_VER_3;
            else if(sd_spec4 == 1)
                EMMCBase->emmc_SCR.emmc_version = SD_VER_4;
        }
    }

    bug("[brcm-emmc] SCR: %08lx-%08lx\n", EMMCBase->emmc_SCR.scr[1], EMMCBase->emmc_SCR.scr[1]);
    bug("[brcm-emmc] SCR version %ld, bus_widths %01lx\n", EMMCBase->emmc_SCR.emmc_version, EMMCBase->emmc_SCR.emmc_bus_widths);

    if(EMMCBase->emmc_SCR.emmc_bus_widths & 0x4)
    {
        // Set 4-bit transfer mode (ACMD6)
        // See HCSS 3.4 for the algorithm
        bug("[brcm-emmc] Switching to 4-bit data mode\n");

        // Disable card interrupt in host
        uint32_t old_irpt_mask = rd32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK);
        uint32_t new_iprt_mask = old_irpt_mask & ~(1 << 8);
        wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, new_iprt_mask);

        // Send ACMD6 to change the card's bit mode
        emmc_cmd(SET_BUS_WIDTH, 0x2, 500000, EMMCBase);
        if(FAIL(EMMCBase))
            bug("[brcm-emmc] Switch to 4-bit data mode failed\n");
        else
        {
            // Change bit mode for Host
            uint32_t control0 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0);
            control0 |= 0x2;
            wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);

            // Re-enable card interrupt in host
            wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, old_irpt_mask);

            bug("[brcm-emmc] Switch to 4-bit complete\n");
        }
    }

    // Get the CMD6 status register
    EMMCBase->emmc_Buffer = &EMMCBase->emmc_StatusReg;
    EMMCBase->emmc_BlocksToTransfer = 1;
    EMMCBase->emmc_BlockSize = 64;
    
    emmc_cmd(SWITCH_FUNC, 0, 500000, EMMCBase);
    
    if (EMMCBase->emmc_DisableHighSpeed == 0 && EMMCBase->emmc_StatusReg[13] & 2)
    {
        bug("[brcm-emmc] Card supports High Speed mode. Switching...\n");

        EMMCBase->emmc_Buffer = &EMMCBase->emmc_StatusReg;
        EMMCBase->emmc_BlocksToTransfer = 1;
        EMMCBase->emmc_BlockSize = 64;

        emmc_cmd(SWITCH_FUNC, 0x80fffff1, 500000, EMMCBase);

        delay(10000, EMMCBase);

        if (EMMCBase->emmc_Overclock != 0)
            emmc_switch_clock_rate(base_clock, EMMCBase->emmc_Overclock, EMMCBase);
        else
            emmc_switch_clock_rate(base_clock, 50000000, EMMCBase);
    }
    
    EMMCBase->emmc_BlockSize = 512;

	bug("[brcm-emmc] Found a valid version %ld SD card\n", EMMCBase->emmc_SCR.emmc_version);

    EMMCBase->emmc_isMicroSD = 1;

	// Reset interrupt register
	wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffffffff);

    return 0;
}

int emmc_card_init(struct EMMCBase *EMMCBase)
{
    ULONG tout;
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

    EMMCBase->emmc_isMicroSD = 0;

    bug("[brcm-emmc] eMMC Card init\n");

    emmc_power_cycle(EMMCBase);

    emmc_reset_host(EMMCBase);

    uint32_t ver = rd32(EMMCBase->emmc_Regs, EMMC_SLOTISR_VER);
    uint32_t vendor = ver >> 24;
    uint32_t sdversion = (ver >> 16) & 0xff;
    uint32_t slot_status = ver & 0xff;
    
    bug("[brcm-emmc] Vendor %lx, sdversion %lx, slot_status %lx\n", vendor, sdversion, slot_status);

    uint32_t control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= (1 << 24);
    // Disable clock
    control1 &= ~(1 << 2);
    control1 &= ~(1 << 0);
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & (0x7 << 24)) == 0, 1000000);
    if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & (7 << 24)) != 0)
    {
        bug("[brcm-emmc] Controller did not reset properly\n");
        return -1;
    }

    bug("[brcm-emmc] control0: %08lx, control1: %08lx, control2: %08lx\n",
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL2));
    
    EMMCBase->emmc_Capabilities0 = rd32(EMMCBase->emmc_Regs, EMMC_CAPABILITIES_0);
    EMMCBase->emmc_Capabilities1 = rd32(EMMCBase->emmc_Regs, EMMC_CAPABILITIES_1);

    bug("[brcm-emmc] Capabilities: %08lx:%08lx\n", EMMCBase->emmc_Capabilities1, EMMCBase->emmc_Capabilities0);

    TIMEOUT_WAIT(rd32(EMMCBase->emmc_Regs, EMMC_STATUS) & (1 << 16), 500000);
    uint32_t status_reg = rd32(EMMCBase->emmc_Regs, EMMC_STATUS);
    if((status_reg & (1 << 16)) == 0)
    {
        bug("[brcm-emmc] No card inserted\n");
        return -1;
    }

    bug("[brcm-emmc] Status: %08lx\n", status_reg);

    // Clear control2
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL2, 0);

    // Set eMMC clock to 200MHz
    set_clock_rate(12, 250000000, EMMCBase);
    set_clock_rate(1, 250000000, EMMCBase);

    // Get the base clock rate
    uint32_t base_clock = get_clock_rate(12, EMMCBase);

    bug("[brcm-emmc] Base clock: %ld Hz\n", base_clock);

    control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= 1;			// enable clock

    // Set to identification frequency (400 kHz)
    uint32_t f_id = emmc_get_clock_divider(base_clock, SD_CLOCK_ID);

    control1 |= f_id;

    control1 |= (7 << 16);		// data timeout = TMCLK * 2^10
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    TIMEOUT_WAIT((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & 0x2), 1000000);
    if((rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1) & 0x2) == 0)
    {
        bug("[brcm-emmc] EMMC: controller's clock did not stabilise within 1 second\n");
        return -1;
    }

    bug("[brcm-emmc] control0: %08lx, control1: %08lx, control2: %08lx\n",
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1), 
            rd32(EMMCBase->emmc_Regs, EMMC_CONTROL2));

    // Enable the SD clock
    delay(2000, EMMCBase);
    control1 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1);
    control1 |= 4;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL1, control1);
    delay(2000, EMMCBase);

    // Mask off sending interrupts to the ARM
    wr32(EMMCBase->emmc_Regs, EMMC_IRPT_EN, 0);
    // Reset interrupts
    wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffffffff);

    // Have all interrupts sent to the INTERRUPT register
    uint32_t irpt_mask = 0xffffffff & (~SD_CARD_INTERRUPT);
#ifdef SD_CARD_INTERRUPTS
    irpt_mask |= SD_CARD_INTERRUPT;
#endif
    wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, irpt_mask);

    delay(2000, EMMCBase);

    bug("[brcm-emmc] Clock enabled, control0: %08lx, control1: %08lx\n",
        rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0),
        rd32(EMMCBase->emmc_Regs, EMMC_CONTROL1));

    // Enable 3.3V on the bus
    uint32_t control0 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0);
    control0 &= 0xffff00ff;
    control0 |= 0x00000e00;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);
    delay(2000, EMMCBase);
    control0 |= 0x00000100;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);
    delay(2000, EMMCBase);

    // Send CMD0 to the card (reset to idle state)
	emmc_cmd(GO_IDLE_STATE, 0, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
        bug("[brcm-emmc] No CMD0 response\n");
        bug("[brcm-emmc] Status: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_STATUS));
        return -1;
	}

    /* Check if it is eMMC or microSD in eMMC slot. Send CMD1, report the host is operating at 3.3V window */
    emmc_cmd(SEND_OP_COND, 0x40FF8000, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
        bug("[brcm-emmc] No CMD1 response\n");
        bug("[brcm-emmc] Status: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_STATUS));
        return emmc_microsd_init(EMMCBase);
	}

    while ((EMMCBase->emmc_Res0 & 0x80000000) == 0) {
        bug("[brcm-emmc] Waiting for card to finish initializadion\n");

        delay(10000, EMMCBase);

        emmc_cmd(SEND_OP_COND, 0x40FF8000, 500000, EMMCBase);
        if(FAIL(EMMCBase))
        {
            bug("[brcm-emmc] No CMD1 response\n");
            bug("[brcm-emmc] Status: %08lx\n", rd32(EMMCBase->emmc_Regs, EMMC_STATUS));
            return -1;
        }
    }

    bug("[brcm-emmc] CMD1 Response: %08lx\n", EMMCBase->emmc_Res0);

	// Send CMD2 to get the cards CID
	emmc_cmd(ALL_SEND_CID, 0, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending ALL_SEND_CID\n");
	    return -1;
	}
	uint32_t card_cid_0 = EMMCBase->emmc_Res0;
	uint32_t card_cid_1 = EMMCBase->emmc_Res1;
	uint32_t card_cid_2 = EMMCBase->emmc_Res2;
	uint32_t card_cid_3 = EMMCBase->emmc_Res3;

    bug("[brcm-emmc] card CID: %08lx%08lx%08lx%08lx\n", card_cid_3, card_cid_2, card_cid_1, card_cid_0);

	// Send CMD3 to enter the data state
	emmc_cmd(SEND_RELATIVE_ADDR, 0x00010000, 500000, EMMCBase);
	if(FAIL(EMMCBase))
    {
        bug("[brcm-emmc] Error sending SEND_RELATIVE_ADDR\n");
        return -1;
    }

	uint32_t cmd3_resp = EMMCBase->emmc_Res0;

	EMMCBase->emmc_CardRCA = 1;
	uint32_t crc_error = (cmd3_resp >> 15) & 0x1;
	uint32_t illegal_cmd = (cmd3_resp >> 14) & 0x1;
	uint32_t error = (cmd3_resp >> 13) & 0x1;
	uint32_t status = (cmd3_resp >> 9) & 0xf;
	uint32_t ready = (cmd3_resp >> 8) & 0x1;

    bug("[brcm-emmc] Res0: %08lx\n", cmd3_resp);

    if(crc_error)
	{
		bug("[brcm-emmc] CRC error\n");
		return -1;
	}

	if(illegal_cmd)
	{
		bug("[brcm-emmc] Illegal command\n");
		return -1;
	}

	if(error)
	{
		bug("[brcm-emmc] Generic error\n");
		return -1;
	}

	if(!ready)
	{
		bug("[brcm-emmc] Not ready for data\n");
		return -1;
	}

    bug("[brcm-emmc] RCA: %04lx\n", EMMCBase->emmc_CardRCA);

	// Now select the card (toggles it to transfer state)
	emmc_cmd(SELECT_CARD, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending CMD7\n");
	    return -1;
	}

    uint32_t cmd7_resp = EMMCBase->emmc_Res0;
	status = (cmd7_resp >> 9) & 0xf;

	if((status != 3) && (status != 4))
	{
		bug("[brcm-emmc] Invalid status (%ld)\n", cmd7_resp);
		return -1;
	}

    bug("[brcm-emmc] Res0: %08lx\n", EMMCBase->emmc_Res0);

    // Set high-speed compatible mode
	emmc_cmd(SWITCH, (185 << 16) | (1 << 8) | (3 << 24), 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending CMD6\n");
	    return -1;
	}
    bug("[brcm-emmc] Res0: %08lx\n", EMMCBase->emmc_Res0);
    emmc_switch_clock_rate(base_clock, EMMC_CLOCK_HIGH, EMMCBase);
    control0 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0);
    control0 |= 0x4;
    wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);
    bug("[brcm-emmc] Clock set to %ld kHz\n", EMMC_CLOCK_HIGH / 1000);

	emmc_cmd(SEND_STATUS, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
	if(FAIL(EMMCBase))
	{
	    bug("[brcm-emmc] Error sending CMD13\n");
	    return -1;
	}
    bug("[brcm-emmc] Res0: %08lx\n", EMMCBase->emmc_Res0);


    bug("[brcm-emmc] Switching to 8-bit data mode\n");

    // Disable card interrupt in host
    uint32_t old_irpt_mask = rd32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK);
    uint32_t new_iprt_mask = old_irpt_mask & ~(1 << 8);
    wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, new_iprt_mask);

    emmc_cmd(SWITCH, (183 << 16) | (2 << 8) | (3 << 24), 500000, EMMCBase);
    if(FAIL(EMMCBase))
        bug("[brcm-emmc] switch to 8-bit data mode failed\n");
    else
    {
        // Change bit mode for Host
        uint32_t control0 = rd32(EMMCBase->emmc_Regs, EMMC_CONTROL0);
        control0 |= 0x2 | (1 << 5);
        wr32(EMMCBase->emmc_Regs, EMMC_CONTROL0, control0);

        // Re-enable card interrupt in host
        wr32(EMMCBase->emmc_Regs, EMMC_IRPT_MASK, old_irpt_mask);

        bug("[brcm-emmc] switch to 8-bit data mode complete\n");
    }

	// Reset interrupt register
	wr32(EMMCBase->emmc_Regs, EMMC_INTERRUPT, 0xffffffff);

    // Set block size
    EMMCBase->emmc_BlockSize = 512;
    emmc_cmd(SET_BLOCKLEN, 512, 500000, EMMCBase);
    if(FAIL(EMMCBase))
    {
        bug("[brcm-emmc] error sending SET_BLOCKLEN\n", NULL);
        return -1;
    }

    return 0;
}


int ensure_data_mode(struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

	if(EMMCBase->emmc_CardRCA == 0)
	{
		// Try again to initialise the card
		int ret = emmc_card_init(EMMCBase);
		if(ret != 0)
			return ret;
	}

    emmc_cmd(SEND_STATUS, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
    if(FAIL(EMMCBase))
    {
        bug("[brcm-emmc] ensure_data_mode() error sending CMD13\n");
        EMMCBase->emmc_CardRCA = 0;
        return -1;
    }

	uint32_t status = EMMCBase->emmc_Res0;
	uint32_t cur_state = (status >> 9) & 0xf;

	if(cur_state == 3)
	{
		// Currently in the stand-by state - select it
		emmc_cmd(SELECT_CARD, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
		if(FAIL(EMMCBase))
		{
			bug("[brcm-emmc] ensure_data_mode() no response from CMD7\n");
			EMMCBase->emmc_CardRCA = 0;
			return -1;
		}
	}
	else if(cur_state == 5)
	{
		// In the data transfer state - cancel the transmission
		emmc_cmd(STOP_TRANSMISSION, 0, 500000, EMMCBase);
		if(FAIL(EMMCBase))
		{
			bug("[brcm-emmc] ensure_data_mode() no response from CMD12\n");
			EMMCBase->emmc_CardRCA = 0;
			return -1;
		}

		// Reset the data circuit
		emmc_reset_dat(EMMCBase);
	}
	else if(cur_state != 4)
	{
		// Not in the transfer state - re-initialise
		int ret = emmc_card_init(EMMCBase);
		if(ret != 0)
			return ret;
	}

	// Check again that we're now in the correct mode
	if(cur_state != 4)
	{
		bug("[brcm-emmc] ensure_data_mode() rechecking status: ");
        emmc_cmd(SEND_STATUS, EMMCBase->emmc_CardRCA << 16, 500000, EMMCBase);
        if(FAIL(EMMCBase))
		{
			bug("no response from CMD13\n");
			EMMCBase->emmc_CardRCA = 0;
			return -1;
		}
		status = EMMCBase->emmc_Res0;
		cur_state = (status >> 9) & 0xf;

		bug("%ld\n", cur_state);

		if(cur_state != 4)
		{
			bug("[brcm-emmc] unable to initialise SD card to "
					"data mode (state %ld)\n", cur_state);
			EMMCBase->emmc_CardRCA = 0;
			return -1;
		}
	}

	return 0;
}

static int emmc_do_data_command(int is_write, uint8_t *buf, uint32_t buf_size, uint32_t block_no, struct EMMCBase *EMMCBase)
{
    struct ExecBase *SysBase = EMMCBase->emmc_SysBase;

	// This is as per HCSS 3.7.2.1
	if(buf_size < EMMCBase->emmc_BlockSize)
	{
        bug("[brcm-emmc] do_data_command() called with buffer size (%ld) less than "
            "block size (%ld)\n", buf_size, EMMCBase->emmc_BlockSize);
        return -1;
	}

	EMMCBase->emmc_BlocksToTransfer = buf_size / EMMCBase->emmc_BlockSize;
	if(buf_size % EMMCBase->emmc_BlockSize)
	{
        bug("[brcm-emmc] do_data_command() called with buffer size (%ld) not an "
            "exact multiple of block size (%ld)\n", buf_size, EMMCBase->emmc_BlockSize);
        return -1;
	}
	EMMCBase->emmc_Buffer = buf;

	// Decide on the command to use
	int command;
	if(is_write)
	{
	    if(EMMCBase->emmc_BlocksToTransfer > 1)
            command = WRITE_MULTIPLE_BLOCK;
        else
            command = WRITE_BLOCK;
	}
	else
    {
        if(EMMCBase->emmc_BlocksToTransfer > 1)
            command = READ_MULTIPLE_BLOCK;
        else
            command = READ_SINGLE_BLOCK;
    }

	int retry_count = 0;
	int max_retries = 5;
	while(retry_count < max_retries)
	{
        emmc_cmd(command, block_no, 5000000, EMMCBase);

        if(SUCCESS(EMMCBase))
            break;
        else
        {
            // In the data transfer state - cancel the transmission
            emmc_cmd(STOP_TRANSMISSION, 0, 500000, EMMCBase);
            if(FAIL(EMMCBase))
            {
                EMMCBase->emmc_CardRCA = 0;
                return -1;
            }

            // Reset the data circuit
            emmc_reset_dat(EMMCBase);
            //RawDoFmt("SD: error sending CMD%ld, ", &command, (APTR)putch, NULL);
            //RawDoFmt("error = %08lx.  ", &SDCardBase->sd_LastError, (APTR)putch, NULL);
            retry_count++;
            /*
            if(retry_count < max_retries)
                RawDoFmt("Retrying...\n", NULL, (APTR)putch, NULL);
            else
                RawDoFmt("Giving up.\n", NULL, (APTR)putch, NULL);
            */
        }
	}
	if(retry_count == max_retries)
    {
        EMMCBase->emmc_CardRCA = 0;
        return -1;
    }

    return 0;
}

int emmc_read(uint8_t *buf, uint32_t buf_size, uint32_t block_no, struct EMMCBase *EMMCBase)
{
	// Check the status of the card
    if(ensure_data_mode(EMMCBase) != 0)
        return -1;

    if(emmc_do_data_command(0, buf, buf_size, block_no, EMMCBase) < 0)
        return -1;

	return buf_size;
}

int emmc_write(uint8_t *buf, uint32_t buf_size, uint32_t block_no, struct EMMCBase *EMMCBase)
{
	// Check the status of the card
    if(ensure_data_mode(EMMCBase) != 0)
        return -1;

    if(emmc_do_data_command(1, buf, buf_size, block_no, EMMCBase) < 0)
        return -1;

	return buf_size;
}
