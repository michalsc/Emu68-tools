#ifndef __EMMC_H
#define __EMMC_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <devices/timer.h>
#include <libraries/configvars.h>
#include <stdint.h>

#define EMMC_VERSION  1
#define EMMC_REVISION 0
#define EMMC_PRIORITY 20

#define USE_BUSY_TIMER  1

struct emmc_scr
{
    uint32_t    scr[2];
    uint32_t    emmc_bus_widths;
    int         emmc_version;
    int         emmc_commands;
};

struct EMMCUnit;

struct EMMCBase {
    struct Device       emmc_Device;
    struct ExecBase *   emmc_SysBase;
    APTR                emmc_ROMBase;
    APTR                emmc_DeviceTreeBase;
    APTR                emmc_MailBox;
    APTR                emmc_Regs;
    ULONG *             emmc_Request;
    APTR                emmc_RequestBase;
    ULONG               emmc_SDHCClock;

    struct ConfigDev *  emmc_ConfigDev;

    struct EMMCUnit *   emmc_Units[5];    /* 5 units at most for the case where SDCard has 4 primary partitions type 0x76 */
    UWORD               emmc_UnitCount;

    struct SignalSemaphore emmc_Lock;
    struct timerequest  emmc_TimeReq;
    struct MsgPort      emmc_Port;

    struct emmc_scr     emmc_SCR;

    UWORD               emmc_BlockSize;
    UWORD               emmc_BlocksToTransfer;
    APTR                emmc_Buffer;

    ULONG               emmc_Res0;
    ULONG               emmc_Res1;
    ULONG               emmc_Res2;
    ULONG               emmc_Res3;

    ULONG               emmc_CID[4];
    UBYTE               emmc_StatusReg[64];
    CONST_STRPTR        emmc_ManuID[255];

    ULONG               emmc_Capabilities0;
    ULONG               emmc_Capabilities1;
    ULONG               emmc_LastCMD;
    ULONG               emmc_LastCMDSuccess;
    ULONG               emmc_LastError;
    ULONG               emmc_LastInterrupt;
    ULONG               emmc_CardRCA;
    ULONG               emmc_CardRemoval;
    ULONG               emmc_FailedVoltageSwitch;
    ULONG               emmc_CardOCR;
    ULONG               emmc_CardSupportsSDHC;
    ULONG               emmc_Overclock;

    UBYTE               emmc_DisableHighSpeed;
    UBYTE               emmc_InCommand;
    UBYTE               emmc_AppCommand;
    UBYTE               emmc_HideUnit0;
    UBYTE               emmc_ReadOnlyUnit0;
    UBYTE               emmc_Verbose;
    UBYTE               emmc_isMicroSD;

    struct Interrupt    emmc_Interrupt;
};

struct EMMCUnit {
    struct Unit         su_Unit;
    struct EMMCBase *   su_Base;
    uint32_t            su_StartBlock;
    uint32_t            su_BlockCount;
    uint8_t             su_UnitNum;
    uint8_t             su_ReadOnly;
    struct Task *       su_Caller;
};

void UnitTask();

#define UNIT_TASK_PRIORITY  10
#define UNIT_TASK_STACKSIZE 16384

#define BASE_NEG_SIZE   (6 * 6)
#define BASE_POS_SIZE   (sizeof(struct EMMCBase))

/* EMMC regs */

#define	EMMC_ARG2		0
#define EMMC_BLKSIZECNT		4
#define EMMC_ARG1		8
#define EMMC_CMDTM		0xC
#define EMMC_RESP0		0x10
#define EMMC_RESP1		0x14
#define EMMC_RESP2		0x18
#define EMMC_RESP3		0x1C
#define EMMC_DATA		0x20
#define EMMC_STATUS		0x24
#define EMMC_CONTROL0		0x28
#define EMMC_CONTROL1		0x2C
#define EMMC_INTERRUPT		0x30
#define EMMC_IRPT_MASK		0x34
#define EMMC_IRPT_EN		0x38
#define EMMC_CONTROL2		0x3C
#define EMMC_CAPABILITIES_0	0x40
#define EMMC_CAPABILITIES_1	0x44
#define EMMC_FORCE_IRPT		0x50
#define EMMC_BOOT_TIMEOUT	0x70
#define EMMC_DBG_SEL		0x74
#define EMMC_EXRDFIFO_CFG	0x80
#define EMMC_EXRDFIFO_EN	0x84
#define EMMC_TUNE_STEP		0x88
#define EMMC_TUNE_STEPS_STD	0x8C
#define EMMC_TUNE_STEPS_DDR	0x90
#define EMMC_SPI_INT_SPT	0xF0
#define EMMC_SLOTISR_VER	0xFC

#define SD_CLOCK_ID         400000
#define SD_CLOCK_NORMAL     25000000
#define SD_CLOCK_HIGH       50000000

#define EMMC_CLOCK_ID         400000
#define EMMC_CLOCK_NORMAL     26000000
#define EMMC_CLOCK_HIGH       75000000

#define SD_CMD_INDEX(a)		((a) << 24)
#define SD_CMD_TYPE_NORMAL	0x0
#define SD_CMD_TYPE_SUSPEND	(1 << 22)
#define SD_CMD_TYPE_RESUME	(2 << 22)
#define SD_CMD_TYPE_ABORT	(3 << 22)
#define SD_CMD_TYPE_MASK    (3 << 22)
#define SD_CMD_ISDATA		(1 << 21)
#define SD_CMD_IXCHK_EN		(1 << 20)
#define SD_CMD_CRCCHK_EN	(1 << 19)
#define SD_CMD_RSPNS_TYPE_NONE	0			// For no response
#define SD_CMD_RSPNS_TYPE_136	(1 << 16)		// For response R2 (with CRC), R3,4 (no CRC)
#define SD_CMD_RSPNS_TYPE_48	(2 << 16)		// For responses R1, R5, R6, R7 (with CRC)
#define SD_CMD_RSPNS_TYPE_48B	(3 << 16)		// For responses R1b, R5b (with CRC)
#define SD_CMD_RSPNS_TYPE_MASK  (3 << 16)
#define SD_CMD_MULTI_BLOCK	(1 << 5)
#define SD_CMD_DAT_DIR_HC	0
#define SD_CMD_DAT_DIR_CH	(1 << 4)
#define SD_CMD_AUTO_CMD_EN_NONE	0
#define SD_CMD_AUTO_CMD_EN_CMD12	(1 << 2)
#define SD_CMD_AUTO_CMD_EN_CMD23	(2 << 2)
#define SD_CMD_BLKCNT_EN		(1 << 1)
#define SD_CMD_DMA          1

#define SD_ERR_CMD_TIMEOUT	0
#define SD_ERR_CMD_CRC		1
#define SD_ERR_CMD_END_BIT	2
#define SD_ERR_CMD_INDEX	3
#define SD_ERR_DATA_TIMEOUT	4
#define SD_ERR_DATA_CRC		5
#define SD_ERR_DATA_END_BIT	6
#define SD_ERR_CURRENT_LIMIT	7
#define SD_ERR_AUTO_CMD12	8
#define SD_ERR_ADMA		9
#define SD_ERR_TUNING		10
#define SD_ERR_RSVD		11

#define SD_ERR_MASK_CMD_TIMEOUT		(1 << (16 + SD_ERR_CMD_TIMEOUT))
#define SD_ERR_MASK_CMD_CRC		(1 << (16 + SD_ERR_CMD_CRC))
#define SD_ERR_MASK_CMD_END_BIT		(1 << (16 + SD_ERR_CMD_END_BIT))
#define SD_ERR_MASK_CMD_INDEX		(1 << (16 + SD_ERR_CMD_INDEX))
#define SD_ERR_MASK_DATA_TIMEOUT	(1 << (16 + SD_ERR_CMD_TIMEOUT))
#define SD_ERR_MASK_DATA_CRC		(1 << (16 + SD_ERR_CMD_CRC))
#define SD_ERR_MASK_DATA_END_BIT	(1 << (16 + SD_ERR_CMD_END_BIT))
#define SD_ERR_MASK_CURRENT_LIMIT	(1 << (16 + SD_ERR_CMD_CURRENT_LIMIT))
#define SD_ERR_MASK_AUTO_CMD12		(1 << (16 + SD_ERR_CMD_AUTO_CMD12))
#define SD_ERR_MASK_ADMA		(1 << (16 + SD_ERR_CMD_ADMA))
#define SD_ERR_MASK_TUNING		(1 << (16 + SD_ERR_CMD_TUNING))

#define SD_COMMAND_COMPLETE     1
#define SD_TRANSFER_COMPLETE    (1 << 1)
#define SD_BLOCK_GAP_EVENT      (1 << 2)
#define SD_DMA_INTERRUPT        (1 << 3)
#define SD_BUFFER_WRITE_READY   (1 << 4)
#define SD_BUFFER_READ_READY    (1 << 5)
#define SD_CARD_INSERTION       (1 << 6)
#define SD_CARD_REMOVAL         (1 << 7)
#define SD_CARD_INTERRUPT       (1 << 8)

#define SUCCESS(a)          (a->emmc_LastCMDSuccess)
#define FAIL(a)             (a->emmc_LastCMDSuccess == 0)
#define TIMEOUT(a)          (FAIL(a) && (a->emmc_LastError == 0))
#define CMD_TIMEOUT(a)      (FAIL(a) && (a->emmc_LastError & (1 << 16)))
#define CMD_CRC(a)          (FAIL(a) && (a->emmc_LastError & (1 << 17)))
#define CMD_END_BIT(a)      (FAIL(a) && (a->emmc_LastError & (1 << 18)))
#define CMD_INDEX(a)        (FAIL(a) && (a->emmc_LastError & (1 << 19)))
#define DATA_TIMEOUT(a)     (FAIL(a) && (a->emmc_LastError & (1 << 20)))
#define DATA_CRC(a)         (FAIL(a) && (a->emmc_LastError & (1 << 21)))
#define DATA_END_BIT(a)     (FAIL(a) && (a->emmc_LastError & (1 << 22)))
#define CURRENT_LIMIT(a)    (FAIL(a) && (a->emmc_LastError & (1 << 23)))
#define ACMD12_ERROR(a)     (FAIL(a) && (a->emmc_LastError & (1 << 24)))
#define ADMA_ERROR(a)       (FAIL(a) && (a->emmc_LastError & (1 << 25)))
#define TUNING_ERROR(a)     (FAIL(a) && (a->emmc_LastError & (1 << 26)))

#define SD_RESP_NONE        SD_CMD_RSPNS_TYPE_NONE
#define SD_RESP_R1          (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R1b         (SD_CMD_RSPNS_TYPE_48B | SD_CMD_CRCCHK_EN)
#define SD_RESP_R2          (SD_CMD_RSPNS_TYPE_136 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R3          SD_CMD_RSPNS_TYPE_48
#define SD_RESP_R4          SD_CMD_RSPNS_TYPE_136
#define SD_RESP_R5          (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R5b         (SD_CMD_RSPNS_TYPE_48B | SD_CMD_CRCCHK_EN)
#define SD_RESP_R6          (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R7          (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)

#define SD_DATA_READ        (SD_CMD_ISDATA | SD_CMD_DAT_DIR_CH)
#define SD_DATA_WRITE       (SD_CMD_ISDATA | SD_CMD_DAT_DIR_HC)

#define SD_CMD_RESERVED(a)  0xffffffff

#define ACMD_6              (SD_CMD_INDEX(6) | SD_RESP_R1)
#define ACMD_13             (SD_CMD_INDEX(13) | SD_RESP_R1)
#define ACMD_22             (SD_CMD_INDEX(22) | SD_RESP_R1 | SD_DATA_READ)
#define ACMD_23             (SD_CMD_INDEX(23) | SD_RESP_R1)
#define ACMD_41             (SD_CMD_INDEX(41) | SD_RESP_R3)
#define ACMD_42             (SD_CMD_INDEX(42) | SD_RESP_R1)
#define ACMD_51             (SD_CMD_INDEX(51) | SD_RESP_R1 | SD_DATA_READ)

#define IS_APP_CMD              0x80000000
#define ACMD(a)                 (a | IS_APP_CMD)
#define SET_BUS_WIDTH           (ACMD_6 | IS_APP_CMD)
#define SD_STATUS               (ACMD_13 | IS_APP_CMD)
#define SEND_NUM_WR_BLOCKS      (ACMD_22 | IS_APP_CMD)
#define SET_WR_BLK_ERASE_COUNT  (ACMD_23 | IS_APP_CMD)
#define SD_SEND_OP_COND         (ACMD_41 | IS_APP_CMD)
#define SET_CLR_CARD_DETECT     (ACMD_42 | IS_APP_CMD)
#define SEND_SCR                (ACMD_51 | IS_APP_CMD)

#define CMD_0               (SD_CMD_INDEX(0))
#define CMD_1               (SD_CMD_INDEX(1) | SD_RESP_R3)
#define CMD_2               (SD_CMD_INDEX(2) | SD_RESP_R2)
#define CMD_3               (SD_CMD_INDEX(3) | SD_RESP_R6)
#define CMD_4               (SD_CMD_INDEX(4))
#define CMD_5               (SD_CMD_INDEX(5) | SD_RESP_R4)
#define CMD_6               (SD_CMD_INDEX(6) | SD_RESP_R1b)
#define CMD_6_SD            (SD_CMD_INDEX(6) | SD_RESP_R1 | SD_DATA_READ)
#define CMD_7               (SD_CMD_INDEX(7) | SD_RESP_R1b)
#define CMD_7nr             (SD_CMD_INDEX(7))
#define CMD_8               (SD_CMD_INDEX(8) | SD_RESP_R7)
#define CMD_9               (SD_CMD_INDEX(9) | SD_RESP_R2)
#define CMD_10              (SD_CMD_INDEX(10) | SD_RESP_R2)
#define CMD_11              (SD_CMD_INDEX(11) | SD_RESP_R1)
#define CMD_12              (SD_CMD_INDEX(12) | SD_RESP_R1b | SD_CMD_TYPE_ABORT)
#define CMD_13              (SD_CMD_INDEX(13) | SD_RESP_R1)
#define CMD_14              (SD_CMD_RESERVED(14))
#define CMD_15              (SD_CMD_INDEX(15))
#define CMD_16              (SD_CMD_INDEX(16) | SD_RESP_R1)
#define CMD_17              (SD_CMD_INDEX(17) | SD_RESP_R1 | SD_DATA_READ)
#define CMD_18              (SD_CMD_INDEX(18) | SD_RESP_R1 | SD_DATA_READ | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN | SD_CMD_AUTO_CMD_EN_CMD12)
#define CMD_19              (SD_CMD_INDEX(19) | SD_RESP_R1 | SD_DATA_READ)
#define CMD_20              (SD_CMD_INDEX(20) | SD_RESP_R1b)
#define CMD_23              (SD_CMD_INDEX(23) | SD_RESP_R1)
#define CMD_24              (SD_CMD_INDEX(24) | SD_RESP_R1 | SD_DATA_WRITE)
#define CMD_25              (SD_CMD_INDEX(25) | SD_RESP_R1 | SD_DATA_WRITE | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN | SD_CMD_AUTO_CMD_EN_CMD12)
#define CMD_27              (SD_CMD_INDEX(27) | SD_RESP_R1 | SD_DATA_WRITE)
#define CMD_28              (SD_CMD_INDEX(28) | SD_RESP_R1b)
#define CMD_29              (SD_CMD_INDEX(29) | SD_RESP_R1b)
#define CMD_30              (SD_CMD_INDEX(30) | SD_RESP_R1 | SD_DATA_READ)
#define CMD_32              (SD_CMD_INDEX(32) | SD_RESP_R1)
#define CMD_33              (SD_CMD_INDEX(33) | SD_RESP_R1)
#define CMD_38              (SD_CMD_INDEX(38) | SD_RESP_R1b)
#define CMD_42              (SD_CMD_RESERVED(42) | SD_RESP_R1)
#define CMD_55              (SD_CMD_INDEX(55) | SD_RESP_R1)
#define CMD_56              (SD_CMD_INDEX(56) | SD_RESP_R1 | SD_CMD_ISDATA)

#define GO_IDLE_STATE           CMD_0
#define SEND_OP_COND            CMD_1
#define ALL_SEND_CID            CMD_2
#define SEND_RELATIVE_ADDR      CMD_3
#define SET_DSR                 CMD_4
#define IO_SET_OP_COND          CMD_5
#define SWITCH_FUNC             CMD_6_SD
#define SWITCH                  CMD_6
#define SELECT_CARD             CMD_7
#define DESELECT_CARD           CMD_7nr
#define SELECT_DESELECT_CARD    CMD_7
#define SEND_IF_COND            CMD_8
#define SEND_EXT_CSD            (SD_CMD_INDEX(8) | SD_RESP_R1 | SD_DATA_READ)
#define SEND_CSD                CMD_9
#define SEND_CID                CMD_10
#define VOLTAGE_SWITCH          CMD_11
#define STOP_TRANSMISSION       CMD_12
#define SEND_STATUS             CMD_13
#define GO_INACTIVE_STATE       CMD_15
#define SET_BLOCKLEN            CMD_16
#define READ_SINGLE_BLOCK       CMD_17
#define READ_MULTIPLE_BLOCK     CMD_18
#define SEND_TUNING_BLOCK       CMD_19
#define SPEED_CLASS_CONTROL     CMD_20
#define SET_BLOCK_COUNT         CMD_23
#define WRITE_BLOCK             CMD_24
#define WRITE_MULTIPLE_BLOCK    CMD_25
#define PROGRAM_CSD             CMD_27
#define SET_WRITE_PROT          CMD_28
#define CLR_WRITE_PROT          CMD_29
#define SEND_WRITE_PROT         CMD_30
#define ERASE_WR_BLK_START      CMD_32
#define ERASE_WR_BLK_END        CMD_33
#define ERASE                   CMD_38
#define LOCK_UNLOCK             CMD_42
#define APP_CMD                 CMD_55
#define GEN_CMD                 CMD_56

#define SD_VER_UNKNOWN      0
#define SD_VER_1            1
#define SD_VER_1_1          2
#define SD_VER_2            3
#define SD_VER_3            4
#define SD_VER_4            5
#define SD_VER_5            6
#define SD_VER_6            7
#define SD_VER_7            8
#define SD_VER_8            9

#define SD_CMD20_SUPP       1
#define SD_CMD23_SUPP       2
#define SD_CMD48_49_SUPP    4
#define SD_CMD58_59_SUPP    8

#define SD_RESET_CMD            (1 << 25)
#define SD_RESET_DAT            (1 << 26)
#define SD_RESET_ALL            (1 << 24)

/* Endian support */

static inline uint64_t LE64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint32_t LE32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t LE16(uint16_t x) { return __builtin_bswap16(x); }

static inline ULONG rd32(APTR addr, ULONG offset)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    ULONG val = LE32(*(volatile ULONG *)addr_off);
    asm volatile("nop");
    return val;
}

static inline void wr32(APTR addr, ULONG offset, ULONG val)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    *(volatile ULONG *)addr_off = LE32(val);
    asm volatile("nop");
}

static inline ULONG rd32be(APTR addr, ULONG offset)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    ULONG val = *(volatile ULONG *)addr_off;
    asm volatile("nop");
    return val;
}

static inline void wr32be(APTR addr, ULONG offset, ULONG val)
{
    APTR addr_off = (APTR)((ULONG)addr + offset);
    *(volatile ULONG *)addr_off = val;
    asm volatile("nop");
}

/* Misc */

static inline void putch(UBYTE data asm("d0"), APTR ignore asm("a3"))
{
    *(UBYTE*)0xdeadbeef = data;
}

void kprintf(const char * msg asm("a0"), void * args asm("a1"));
void delay(ULONG us, struct EMMCBase *EMMCBase);
ULONG EMMC_Expunge(struct EMMCBase * EMMCBase asm("a6"));
APTR EMMC_ExtFunc(struct EMMCBase * EMMCBase asm("a6"));
void EMMC_Open(struct IORequest * io asm("a1"), LONG unitNumber asm("d0"), ULONG flags asm("d1"));
ULONG EMMC_Close(struct IORequest * io asm("a1"));
void EMMC_BeginIO(struct IORequest *io asm("a1"));
LONG EMMC_AbortIO(struct IORequest *io asm("a1"));

#define bug(string, ...) \
    do { ULONG args[] = {0, __VA_ARGS__}; kprintf(string, &args[1]); } while(0)

#define TIMEOUT_WAIT(check_func, tout) \
    do { ULONG cnt = (tout) / 2; if (cnt == 0) cnt = 1; while(cnt != 0) { if (check_func) break; \
    cnt = cnt - 1; delay(2, EMMCBase); }  } while(0)

void led(int on, struct EMMCBase *EMMCBase);
void int_do_io(struct IORequest *io , struct EMMCBase * EMMCBase);
void emmc_cmd(ULONG command, ULONG arg, ULONG timeout, struct EMMCBase *EMMCBase);
int emmc_card_init(struct EMMCBase *EMMCBase);
int emmc_read(uint8_t *buf, uint32_t buf_size, uint32_t block_no, struct EMMCBase *EMMCBase);
int emmc_write(uint8_t *buf, uint32_t buf_size, uint32_t block_no, struct EMMCBase *EMMCBase);

#endif /* __EMMC_H */
