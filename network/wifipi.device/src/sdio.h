#ifndef _SDIO_H
#define _SDIO_H

#include <exec/execbase.h>
#include <exec/types.h>
#include <exec/semaphores.h>
#include <devices/sana2.h>
#include <stdint.h>

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
#define SD_CLOCK_NORMAL     26000000
#define SD_CLOCK_HIGH       52000000

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

#define SUCCESS(a)          (a->s_LastCMDSuccess)
#define FAIL(a)             (a->s_LastCMDSuccess == 0)
#define TIMEOUT(a)          (FAIL(a) && (a->s_LastError == 0))
#define CMD_TIMEOUT(a)      (FAIL(a) && (a->s_LastError & (1 << 16)))
#define CMD_CRC(a)          (FAIL(a) && (a->s_LastError & (1 << 17)))
#define CMD_END_BIT(a)      (FAIL(a) && (a->s_LastError & (1 << 18)))
#define CMD_INDEX(a)        (FAIL(a) && (a->s_LastError & (1 << 19)))
#define DATA_TIMEOUT(a)     (FAIL(a) && (a->s_LastError & (1 << 20)))
#define DATA_CRC(a)         (FAIL(a) && (a->s_LastError & (1 << 21)))
#define DATA_END_BIT(a)     (FAIL(a) && (a->s_LastError & (1 << 22)))
#define CURRENT_LIMIT(a)    (FAIL(a) && (a->s_LastError & (1 << 23)))
#define ACMD12_ERROR(a)     (FAIL(a) && (a->s_LastError & (1 << 24)))
#define ADMA_ERROR(a)       (FAIL(a) && (a->s_LastError & (1 << 25)))
#define TUNING_ERROR(a)     (FAIL(a) && (a->s_LastError & (1 << 26)))

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
#define SD_RESP_R4mod       (SD_CMD_RSPNS_TYPE_48)
#define SD_RESP_R6mod       (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)

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
#define CMD_2               (SD_CMD_INDEX(2) | SD_RESP_R2)
#define CMD_3               (SD_CMD_INDEX(3) | SD_RESP_R6)
#define CMD_4               (SD_CMD_INDEX(4))
#define CMD_5               (SD_CMD_INDEX(5) | SD_RESP_R4mod)
#define CMD_6               (SD_CMD_INDEX(6) | SD_RESP_R1 | SD_DATA_READ)
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
#define CMD_52              (SD_CMD_INDEX(52) | SD_RESP_R5)
#define CMD_53              (SD_CMD_INDEX(53) | SD_RESP_R5)
#define CMD_55              (SD_CMD_INDEX(55) | SD_RESP_R1)
#define CMD_56              (SD_CMD_INDEX(56) | SD_RESP_R1 | SD_CMD_ISDATA)

#define GO_IDLE_STATE           CMD_0
#define ALL_SEND_CID            CMD_2
#define SEND_RELATIVE_ADDR      CMD_3
#define SET_DSR                 CMD_4
#define IO_SET_OP_COND          CMD_5
#define SWITCH_FUNC             CMD_6
#define SELECT_CARD             CMD_7
#define DESELECT_CARD           CMD_7nr
#define SELECT_DESELECT_CARD    CMD_7
#define SEND_IF_COND            CMD_8
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
#define IO_RW_DIRECT            CMD_52
#define IO_RW_EXTENDED          CMD_53
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

#define SD_FUNC_BUS         0
#define SD_FUNC_CIA         0
#define SD_FUNC_BAK         1
#define SD_FUNC_RAD         2

#include "zw_regs.h"

#define SDIO_FBR_ADDR(func, reg)    (((func) << 8) | (reg))

struct WiFiBase;

/* clkstate */
#define CLK_NONE	0
#define CLK_SDONLY	1
#define CLK_PENDING	2
#define CLK_AVAIL	3

#define S_LOCK(sdio) do { ObtainSemaphore(&(sdio)->s_Lock); } while(0)
#define S_UNLOCK(sdio) do { ReleaseSemaphore(&(sdio)->s_Lock); } while(0)

struct SDIO {
    struct WiFiBase *   s_WiFiBase;
    struct ExecBase *   s_SysBase;
    struct Task *       s_ScannerTask;
    struct Task *       s_ReceiverTask;
    struct MsgPort *    s_ReceiverPort;
    struct MsgPort *    s_SenderPort;
    struct MinList *    s_CtrlWaitList;
    struct IOSana2Req * s_ScanRequest;

    struct SignalSemaphore s_Lock;
    APTR                s_SDIO;
    ULONG               s_CardRCA;
    ULONG               s_LastCMD;
    UBYTE               s_LastCMDSuccess;
    UBYTE               s_ClkState;
    UBYTE               s_ALPOnly;
    ULONG               s_LastBackplaneWindow;
    ULONG               s_BlockSize;
    ULONG               s_BlocksToTransfer;
    ULONG               s_LastError;
    ULONG               s_LastInterrupt;
    ULONG               s_Res0;
    ULONG               s_Res1;
    ULONG               s_Res2;
    ULONG               s_Res3;
    ULONG               s_HostINTMask;
    APTR                s_Buffer;

    APTR                s_TXBuffer;
    APTR                s_RXBuffer;

    UBYTE               s_MaxTXSeq;
    UBYTE               s_TXSeq;
    UBYTE               s_RXSeq;
    UWORD               s_CmdID;
    BOOL                s_GlomEnabled;

    ULONG lastCmd;
    ULONG lastArg;
    ULONG lastBlockSize;
    ULONG lastBlockCount;

    struct Core *       s_CC;       // Chipcomm core
    struct Core *       s_SDIOC;    // SDIO core
    struct Chip *       s_Chip;

    int     (*IsError)(struct SDIO *);
    ULONG   (*BackplaneAddr)(ULONG addr, struct SDIO *);
    void    (*WriteByte)(UBYTE function, ULONG address, UBYTE value, struct SDIO *);
    UBYTE   (*ReadByte)(UBYTE function, ULONG address, struct SDIO *);
    void    (*Write)(UBYTE function, ULONG address, void *data, ULONG length, struct SDIO *sdio);
    void    (*Read)(UBYTE function, ULONG address, void *data, ULONG length, struct SDIO *sdio);
    void    (*Write32)(ULONG address, ULONG data, struct SDIO *sdio);
    ULONG   (*Read32)(ULONG address, struct SDIO *sdio);
    int     (*ClkCTRL)(UBYTE target, UBYTE pendingOK, struct SDIO *sdio);
    void    (*SendPKT)(UBYTE *pkt, ULONG length, struct SDIO *);
    void    (*RecvPKT)(UBYTE *pkt, ULONG length, struct SDIO *);
    ULONG   (*GetIntStatus)(struct SDIO *);
};

struct SDIO * sdio_init(struct WiFiBase *WiFiBase);

#endif /* _SDIO_H */
