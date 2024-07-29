#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>

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

/* Agent registers (common for every core) */
#define BCMA_OOB_SEL_OUT_A30		0x0100
#define BCMA_IOCTL			0x0408 /* IO control */
#define  BCMA_IOCTL_CLK			0x0001
#define  BCMA_IOCTL_FGC			0x0002
#define  BCMA_IOCTL_CORE_BITS		0x3FFC
#define  BCMA_IOCTL_PME_EN		0x4000
#define  BCMA_IOCTL_BIST_EN		0x8000
#define BCMA_IOST			0x0500 /* IO status */
#define  BCMA_IOST_CORE_BITS		0x0FFF
#define  BCMA_IOST_DMA64		0x1000
#define  BCMA_IOST_GATED_CLK		0x2000
#define  BCMA_IOST_BIST_ERROR		0x4000
#define  BCMA_IOST_BIST_DONE		0x8000
#define BCMA_RESET_CTL			0x0800
#define  BCMA_RESET_CTL_RESET		0x0001
#define BCMA_RESET_ST			0x0804

#define BCMA_NS_ROM_IOST_BOOT_DEV_MASK	0x0003
#define BCMA_NS_ROM_IOST_BOOT_DEV_NOR	0x0000
#define BCMA_NS_ROM_IOST_BOOT_DEV_NAND	0x0001
#define BCMA_NS_ROM_IOST_BOOT_DEV_ROM	0x0002

static inline void delay_us(ULONG us, struct WiFiBase *WiFiBase)
{
    (void)WiFiBase;
    ULONG timer = LE32(*(volatile ULONG*)0xf2003004);
    ULONG end = timer + us;

    if (end < timer) {
        while (end < LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
    }
    while (end > LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
}

static struct Core *brcm_chip_get_core(struct Chip *chip, UWORD coreID)
{
    struct Core *core;

    ForeachNode(&chip->c_Cores, core)
    {
        if (core->c_CoreID == coreID)
            return core;
    }
    
    return NULL;
}

static void brcm_chip_disable_arm(struct Chip *chip, UWORD id)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    struct Core *core;
    ULONG val;

    core = chip->GetCore(chip, id);
    if (!core)
        return;

    switch (id)
    {
        case BCMA_CORE_ARM_CM3:
            chip->DisableCore(chip, core, 0, 0);
            break;
        
        case BCMA_CORE_ARM_CR4:
        case BCMA_CORE_ARM_CA7:
            /* clear all IOCTL bits except HALT bit */
            val = chip->c_SDIO->Read32(core->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);
            val &= ARMCR4_BCMA_IOCTL_CPUHALT;
            chip->ResetCore(chip, core, val, ARMCR4_BCMA_IOCTL_CPUHALT, ARMCR4_BCMA_IOCTL_CPUHALT);
            break;

        default:
            bug("[WiFi] chip_disable_arm unknown id: %ld\n", id);
            break;
    }
}

static void sdio_activate(struct Core *core, ULONG resetVector)
{
    struct ExecBase *SysBase = core->c_Chip->c_WiFiBase->w_SysBase;

    ULONG reg_addr;
 
    /* clear all interrupts */
    reg_addr = core->c_BaseAddress + SD_REG(intstatus);

    D(bug("[WiFi] sdio_activate, reg_addr = %08lx\n", reg_addr));

    core->c_Chip->c_SDIO->Write32(reg_addr, 0xFFFFFFFF, core->c_Chip->c_SDIO);

    D(bug("[WiFi] reset_vector = %08lx\n", resetVector));

    if (resetVector)
        /* Write reset vector to address 0 */
        core->c_Chip->c_SDIO->Write32(0, resetVector, core->c_Chip->c_SDIO);
}

static void brcm_chip_cm3_set_passive(struct Chip *chip)
{
    struct Core *core;

    brcm_chip_disable_arm(chip, BCMA_CORE_ARM_CM3);
    core = chip->GetCore(chip, BCMA_CORE_80211);
    chip->ResetCore(chip, core, D11_BCMA_IOCTL_PHYRESET |
                    D11_BCMA_IOCTL_PHYCLOCKEN,
                    D11_BCMA_IOCTL_PHYCLOCKEN,
                    D11_BCMA_IOCTL_PHYCLOCKEN);
    core = chip->GetCore(chip, BCMA_CORE_INTERNAL_MEM);
    chip->ResetCore(chip, core, 0, 0, 0);
#if 0
    /* disable bank #3 remap for this device */
    if (chip->pub.chip == BRCM_CC_43430_CHIP_ID ||
        chip->pub.chip == CY_CC_43439_CHIP_ID) {
        sr = container_of(core, struct brcmf_core_priv, pub);
        brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankidx), 3);
        brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankpda), 0);
    }
#endif
}

static BOOL brcm_chip_cm3_set_active(struct Chip *chip, ULONG resetVector)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    struct Core *core;
    (void)resetVector;

    core = chip->GetCore(chip, BCMA_CORE_INTERNAL_MEM);
    if (!chip->IsCoreUp(chip, core)) {
        bug("[WiFi] SOCRAM core is down after reset?\n");
        return FALSE;
    }

    sdio_activate(chip->GetCore(chip, BCMA_CORE_SDIO_DEV), 0);

    core = chip->GetCore(chip, BCMA_CORE_ARM_CM3);
    chip->ResetCore(chip, core, 0, 0, 0);

    return TRUE;
}

struct Core *brcmf_chip_get_d11core(struct Chip *chip, UBYTE unit)
{
    struct Core *core;

    ForeachNode(&chip->c_Cores, core)
    {
        if (core->c_CoreID == BCMA_CORE_80211) {
            if (unit-- == 0)
                return core;
        }
    }
    return NULL;
}

struct Core *brcmf_chip_get_pmu(struct Chip *chip)
{
	struct Core *cc = (struct Core *)chip->c_Cores.mlh_Head;
	struct Core *pmu;

	/* See if there is separated PMU core available */
	if (chip->c_ChipREV >= 35 &&
	    chip->c_CapsExt & CC_CAP_EXT_AOB_PRESENT) {
		pmu = brcm_chip_get_core(chip, BCMA_CORE_PMU);
		if (pmu)
			return pmu;
	}

	/* Fallback to ChipCommon core for older hardware */
	return cc;
}

static void brcm_chip_cr4_set_passive(struct Chip *chip)
{
    int i;
    struct Core *core;

    brcm_chip_disable_arm(chip, BCMA_CORE_ARM_CR4);

    /* Disable the cores only and let the firmware enable them.
        * Releasing reset ourselves breaks BCM4387 in weird ways.
        */
    for (i = 0; (core = brcmf_chip_get_d11core(chip, i)); i++)
        chip->DisableCore(chip, core, D11_BCMA_IOCTL_PHYRESET |
                        D11_BCMA_IOCTL_PHYCLOCKEN,
                        D11_BCMA_IOCTL_PHYCLOCKEN);
}

static BOOL brcm_chip_cr4_set_active(struct Chip *chip, ULONG resetVector)
{
    struct Core *core;

    sdio_activate(chip->GetCore(chip, BCMA_CORE_SDIO_DEV), resetVector);

    /* restore ARM */
    core = chip->GetCore(chip, BCMA_CORE_ARM_CR4);
    chip->ResetCore(chip, core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

    return TRUE;
}

static void brcm_chip_ca7_set_passive(struct Chip *chip)
{
    struct Core *core;

    brcm_chip_disable_arm(chip, BCMA_CORE_ARM_CA7);

    core = chip->GetCore(chip, BCMA_CORE_80211);
    chip->ResetCore(chip, core, D11_BCMA_IOCTL_PHYRESET |
                    D11_BCMA_IOCTL_PHYCLOCKEN,
                    D11_BCMA_IOCTL_PHYCLOCKEN,
                    D11_BCMA_IOCTL_PHYCLOCKEN);
}

static BOOL brcm_chip_ca7_set_active(struct Chip *chip, ULONG resetVector)
{
    struct Core *core;

    sdio_activate(chip->GetCore(chip, BCMA_CORE_SDIO_DEV), resetVector);

    /* restore ARM */
    core = chip->GetCore(chip, BCMA_CORE_ARM_CA7);
    chip->ResetCore(chip, core, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);

    return TRUE;
}

static BOOL brcm_chip_ai_iscoreup(struct Chip *chip, struct Core *core)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    ULONG regdata;
    BOOL ret;

    D(bug("[WiFi] AI:IsCoreUp(%04lx): ", core->c_CoreID));

    regdata = chip->c_SDIO->Read32(core->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);
    ret = (regdata & (BCMA_IOCTL_FGC | BCMA_IOCTL_CLK)) == BCMA_IOCTL_CLK;

    regdata = chip->c_SDIO->Read32(core->c_WrapBase + BCMA_RESET_CTL, chip->c_SDIO);
    ret = ret && ((regdata & BCMA_RESET_CTL_RESET) == 0);

    D({
        if (ret) bug(" UP\n");
        else bug("DOWN\n");
    });

    return ret;
}

static void brcm_chip_ai_disablecore(struct Chip *chip, struct Core *core, ULONG preReset, ULONG reset)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    ULONG regdata;

    D(bug("[WiFi] AI:DisableCore(%04lx, %08lx, %08lx)\n", core->c_CoreID, preReset, reset));

    /* if core is already in reset, skip reset */
    regdata = chip->c_SDIO->Read32(core->c_WrapBase + BCMA_RESET_CTL, chip->c_SDIO);
    if ((regdata & BCMA_RESET_CTL_RESET) == 0)
    {
        /* configure reset */
        chip->c_SDIO->Write32(core->c_WrapBase + BCMA_IOCTL,
                    preReset | BCMA_IOCTL_FGC | BCMA_IOCTL_CLK, chip->c_SDIO);
        chip->c_SDIO->Read32(core->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);

        /* put in reset */
        chip->c_SDIO->Write32(core->c_WrapBase + BCMA_RESET_CTL,
                    BCMA_RESET_CTL_RESET, chip->c_SDIO);
        
        delay_us(20, chip->c_WiFiBase);

        /* wait till reset is 1 */
        int tout = 300;
        while (tout && chip->c_SDIO->Read32(core->c_WrapBase + BCMA_RESET_CTL, chip->c_SDIO) == BCMA_RESET_CTL_RESET) {
            delay_us(100, chip->c_WiFiBase);
            tout--;
        }
    }

    /* in-reset configure */
    chip->c_SDIO->Write32(core->c_WrapBase + BCMA_IOCTL,
                reset | BCMA_IOCTL_FGC | BCMA_IOCTL_CLK, chip->c_SDIO);
    chip->c_SDIO->Read32(core->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);
}

static void brcm_chip_ai_resetcore(struct Chip *chip, struct Core *core, ULONG preReset, ULONG reset, ULONG postReset)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    int count;
    struct Core *d11core2 = NULL;

    D(bug("[WiFi] AI:ResetCore(%04lx, %08lx, %08lx, %08lx)\n", core->c_CoreID, preReset, reset, postReset));

    /* special handle two D11 cores reset */
    if (core->c_CoreID == BCMA_CORE_80211) {
        d11core2 = brcmf_chip_get_d11core(chip, 1);
        if (d11core2) {
            D(bug("[WiFi] found two d11 cores, reset both\n"));
        }
    }

    /* must disable first to work for arbitrary current core state */
    chip->DisableCore(chip, core, preReset, reset);
    if (d11core2)
        chip->DisableCore(chip, d11core2, preReset, reset);

    count = 0;
    while (chip->c_SDIO->Read32(core->c_WrapBase + BCMA_RESET_CTL, chip->c_SDIO) & BCMA_RESET_CTL_RESET)
    {
        chip->c_SDIO->Write32(core->c_WrapBase + BCMA_RESET_CTL, 0, chip->c_SDIO);
        count++;
        if (count > 50)
            break;
        delay_us(60, chip->c_WiFiBase);
    }

    if (d11core2)
    {
        count = 0;
        while (chip->c_SDIO->Read32(d11core2->c_WrapBase + BCMA_RESET_CTL, chip->c_SDIO) & BCMA_RESET_CTL_RESET)
        {
            chip->c_SDIO->Write32(d11core2->c_WrapBase + BCMA_RESET_CTL, 0, chip->c_SDIO);
            count++;
            if (count > 50)
                break;
            delay_us(60, chip->c_WiFiBase);
        }
    }

    chip->c_SDIO->Write32(core->c_WrapBase + BCMA_IOCTL, postReset | BCMA_IOCTL_CLK, chip->c_SDIO);
    chip->c_SDIO->Read32(core->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);

    if (d11core2)
    {
        chip->c_SDIO->Write32(d11core2->c_WrapBase + BCMA_IOCTL, postReset | BCMA_IOCTL_CLK, chip->c_SDIO);
        chip->c_SDIO->Read32(d11core2->c_WrapBase + BCMA_IOCTL, chip->c_SDIO);
    }
}

static int brcm_chip_cores_check(struct Chip * chip)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    struct Core *core;
    UBYTE need_socram = FALSE;
    UBYTE has_socram = FALSE;
    UBYTE cpu_found = FALSE;
    int idx = 1;

    D(bug("[WiFi] Cores check\n"));

    ForeachNode(&chip->c_Cores, core)
    {
        D(bug("[WiFi] Core #%ld 0x%04lx:%03ld base 0x%08lx wrap 0x%08lx",
            idx++, core->c_CoreID, core->c_CoreREV, core->c_BaseAddress, core->c_WrapBase));

        switch (core->c_CoreID)
        {
            case BCMA_CORE_ARM_CM3:
                D(bug(" is ARM CM3, needs SOC RAM"));
                cpu_found = TRUE;
                need_socram = TRUE;
                chip->SetActive = brcm_chip_cm3_set_active;
                chip->SetPassive = brcm_chip_cm3_set_passive;
                break;

            case BCMA_CORE_INTERNAL_MEM:
                has_socram = TRUE;
                D(bug(" is SOC RAM"));
                break;

            case BCMA_CORE_ARM_CR4:
                D(bug(" is ARM_CR4"));
                cpu_found = TRUE;
                chip->SetActive = brcm_chip_cr4_set_active;
                chip->SetPassive = brcm_chip_cr4_set_passive;
                break;

            case BCMA_CORE_ARM_CA7:
                D(bug(" is ARM_CA7"));
                cpu_found = TRUE;
                chip->SetActive = brcm_chip_ca7_set_active;
                chip->SetPassive = brcm_chip_ca7_set_passive;
                break;

            default:
                break;
        }
        D(bug("\n"));
    }

    if (!cpu_found)
    {
        D(bug("[WiFi] CPU core not detected\n"));
        return FALSE;
    }

    /* check RAM core presence for ARM CM3 core */
    if (need_socram && !has_socram)
    {
        D(bug("[WiFi] RAM core not provided with ARM CM3 core\n"));
        return FALSE;
    }
    return TRUE;
}



static int sdio_buscoreprep(struct SDIO *sdio)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    UBYTE clkval, clkset, tout;
    D(bug("[WiFi] sdio_buscoreprep\n"));

    /* Try forcing SDIO core to do ALPAvail request only */
    clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ;
    sdio->BackplaneAddr(SI_ENUM_BASE_DEFAULT, sdio);
    sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, clkset, sdio);

    /* If register supported, wait for ALPAvail and then force ALP */
    /* This may take up to 15 milliseconds */
    clkval = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
    if ((clkval & ~SBSDIO_AVBITS) != clkset) {
        D(bug("ChipClkCSR access: wrote 0x%02lx read 0x%02lx\n",
                clkset, clkval));
        return 0;
    }

    tout = 15;
    do {
        clkval = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
        delay_us(1000, sdio->s_WiFiBase);
        if (--tout == 0)
        {
            
            break;
        }
    } while (!SBSDIO_ALPAV(clkval));

    if (!SBSDIO_ALPAV(clkval)) {
        D(bug("[WiFi] timed out while waiting for ALP ready\n"));
        return 0;
    }

    clkset = SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP;
    sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, clkset, sdio);
    delay_us(65, sdio->s_WiFiBase);

    /* Also, disable the extra SDIO pull-ups */
    sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_SDIOPULLUP, 0, sdio);

    return 1;
}

static ULONG brcm_chip_dmp_get_desc(struct SDIO *sdio, ULONG *eromaddr, UBYTE *type)
{
    ULONG val;

    /* read next descriptor */
    val = sdio->Read32(*eromaddr, sdio);
    *eromaddr += 4;

    if (!type)
        return val;

    /* determine descriptor type */
    *type = (val & DMP_DESC_TYPE_MSK);
    if ((*type & ~DMP_DESC_ADDRSIZE_GT32) == DMP_DESC_ADDRESS)
        *type = DMP_DESC_ADDRESS;

    return val;
}


static int brcm_chip_dmp_get_regaddr(struct SDIO *sdio, ULONG *eromaddr, ULONG *regbase, ULONG *wrapbase)
{
    UBYTE desc;
    ULONG val, szdesc;
    UBYTE stype, sztype, wraptype;

    *regbase = 0;
    *wrapbase = 0;

    val = brcm_chip_dmp_get_desc(sdio, eromaddr, &desc);
    if (desc == DMP_DESC_MASTER_PORT) {
        wraptype = DMP_SLAVE_TYPE_MWRAP;
    } else if (desc == DMP_DESC_ADDRESS) {
        /* revert erom address */
        *eromaddr -= 4;
        wraptype = DMP_SLAVE_TYPE_SWRAP;
    } else {
        *eromaddr -= 4;
        return 0;
    }

    do {
        /* locate address descriptor */
        do {
            val = brcm_chip_dmp_get_desc(sdio, eromaddr, &desc);
            /* unexpected table end */
            if (desc == DMP_DESC_EOT) {
                *eromaddr -= 4;
                return 0;
            }
        } while (desc != DMP_DESC_ADDRESS &&
                desc != DMP_DESC_COMPONENT);

        /* stop if we crossed current component border */
        if (desc == DMP_DESC_COMPONENT) {
            *eromaddr -= 4;
            return 0;
        }

        /* skip upper 32-bit address descriptor */
        if (val & DMP_DESC_ADDRSIZE_GT32)
            brcm_chip_dmp_get_desc(sdio, eromaddr, NULL);

        sztype = (val & DMP_SLAVE_SIZE_TYPE) >> DMP_SLAVE_SIZE_TYPE_S;

        /* next size descriptor can be skipped */
        if (sztype == DMP_SLAVE_SIZE_DESC) {
            szdesc = brcm_chip_dmp_get_desc(sdio, eromaddr, NULL);
            /* skip upper size descriptor if present */
            if (szdesc & DMP_DESC_ADDRSIZE_GT32)
                brcm_chip_dmp_get_desc(sdio, eromaddr, NULL);
        }

        /* look for 4K or 8K register regions */
        if (sztype != DMP_SLAVE_SIZE_4K &&
            sztype != DMP_SLAVE_SIZE_8K)
            continue;

        stype = (val & DMP_SLAVE_TYPE) >> DMP_SLAVE_TYPE_S;

        /* only regular slave and wrapper */
        if (*regbase == 0 && stype == DMP_SLAVE_TYPE_SLAVE)
            *regbase = val & DMP_SLAVE_ADDR_BASE;
        if (*wrapbase == 0 && stype == wraptype)
            *wrapbase = val & DMP_SLAVE_ADDR_BASE;
    } while (*regbase == 0 || *wrapbase == 0);

    return 1;
}


int brcm_chip_dmp_erom_scan(struct Chip * chip)
{
    struct WiFiBase *WiFiBase = chip->c_WiFiBase;
    struct ExecBase *SysBase = WiFiBase->w_SysBase;
    struct SDIO * sdio = chip->c_SDIO;
    ULONG val;
    UBYTE desc_type = 0;
    ULONG eromaddr;
    UWORD id;
    UBYTE nmw, nsw, rev;
    ULONG base, wrap;
    int err;

    D(bug("[WiFi] EROM scan\n"));

    eromaddr = sdio->Read32(CORE_CC_REG(SI_ENUM_BASE_DEFAULT, eromptr), sdio);

    D(bug("[WiFi] EROM base addr: %08lx\n", eromaddr));

    while (desc_type != DMP_DESC_EOT)
    {
        val = brcm_chip_dmp_get_desc(sdio, &eromaddr, &desc_type);

        if (!(val & DMP_DESC_VALID))
            continue;

        if (desc_type == DMP_DESC_EMPTY)
            continue;

        /* need a component descriptor */
        if (desc_type != DMP_DESC_COMPONENT)
            continue;

        id = (val & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;

        /* next descriptor must be component as well */
        val = brcm_chip_dmp_get_desc(sdio, &eromaddr, &desc_type);
        if ((val & DMP_DESC_TYPE_MSK) != DMP_DESC_COMPONENT)
            return 0;

        /* only look at cores with master port(s) */
        nmw = (val & DMP_COMP_NUM_MWRAP) >> DMP_COMP_NUM_MWRAP_S;
        nsw = (val & DMP_COMP_NUM_SWRAP) >> DMP_COMP_NUM_SWRAP_S;
        rev = (val & DMP_COMP_REVISION) >> DMP_COMP_REVISION_S;

        /* need core with ports */
        if (nmw + nsw == 0 &&
            id != BCMA_CORE_PMU &&
            id != BCMA_CORE_GCI)
            continue;

        /* try to obtain register address info */
        err = brcm_chip_dmp_get_regaddr(sdio, &eromaddr, &base, &wrap);
        if (!err)
            continue;

        D(bug("[WiFi] Found core with id=0x%04lx, base=0x%08lx, wrap=0x%08lx\n", id, base, wrap));

        struct Core *core = AllocPooled(WiFiBase->w_MemPool, sizeof(struct Core));
        if (core)
        {
            core->c_Chip = chip;
            core->c_BaseAddress = base;
            core->c_WrapBase = wrap;
            core->c_CoreID = id;
            core->c_CoreREV = rev;

            AddTail((struct List *)&chip->c_Cores, (struct Node *)core);
        }
    }

    return 0;
}

int chip_setup(struct Chip *chip)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    struct Core *cc;
    struct Core *pmu;
    ULONG base;
    ULONG val;
    int ret = 1;

    cc = (struct Core *)chip->c_Cores.mlh_Head;
    base = cc->c_BaseAddress;

    // Remember Chipcomm and SDIO cores in SDIO
    chip->c_SDIO->s_CC = cc;
    chip->c_SDIO->s_SDIOC = chip->GetCore(chip, BCMA_CORE_SDIO_DEV);

    /* get chipcommon capabilites */
    chip->c_Caps = chip->c_SDIO->Read32(CORE_CC_REG(base, capabilities), chip->c_SDIO);
    chip->c_CapsExt = chip->c_SDIO->Read32(CORE_CC_REG(base, capabilities_ext), chip->c_SDIO);

    D(bug("[WiFi] chip_setup\n"));
    D(bug("[WiFi] Chipcomm caps: %08lx, caps ext: %08lx\n", chip->c_Caps, chip->c_CapsExt));

    /* get pmu caps & rev */
    pmu = brcmf_chip_get_pmu(chip); /* after reading cc_caps_ext */
    if (chip->c_Caps & CC_CAP_PMU) {
        val = chip->c_SDIO->Read32(CORE_CC_REG(pmu->c_BaseAddress, pmucapabilities), chip->c_SDIO);
        chip->c_PMURev = val & PCAP_REV_MASK;
        chip->c_PMUCaps = val;
    }

    D(bug("[WiFi] Chipcomm rev=%ld, pmurev=%ld, pmucaps=%08lx\n", cc->c_CoreREV, chip->c_PMURev, chip->c_PMUCaps));

    return ret;
}


/* bankidx and bankinfo reg defines corerev >= 8 */
#define SOCRAM_BANKINFO_RETNTRAM_MASK	0x00010000
#define SOCRAM_BANKINFO_SZMASK		0x0000007f
#define SOCRAM_BANKIDX_ROM_MASK		0x00000100

#define SOCRAM_BANKIDX_MEMTYPE_SHIFT	8
/* socram bankinfo memtype */
#define SOCRAM_MEMTYPE_RAM		0
#define SOCRAM_MEMTYPE_R0M		1
#define SOCRAM_MEMTYPE_DEVRAM		2

#define SOCRAM_BANKINFO_SZBASE		8192
#define SRCI_LSS_MASK		0x00f00000
#define SRCI_LSS_SHIFT		20
#define	SRCI_SRNB_MASK		0xf0
#define	SRCI_SRNB_MASK_EXT	0x100
#define	SRCI_SRNB_SHIFT		4
#define	SRCI_SRBSZ_MASK		0xf
#define	SRCI_SRBSZ_SHIFT	0
#define SR_BSZ_BASE		14

#define ARMCR4_CAP		(0x04)
#define ARMCR4_BANKIDX		(0x40)
#define ARMCR4_BANKINFO		(0x44)
#define ARMCR4_BANKPDA		(0x4C)

#define	ARMCR4_TCBBNB_MASK	0xf0
#define	ARMCR4_TCBBNB_SHIFT	4
#define	ARMCR4_TCBANB_MASK	0xf
#define	ARMCR4_TCBANB_SHIFT	0

#define	ARMCR4_BSZ_MASK		0x7f
#define	ARMCR4_BSZ_MULT		8192
#define	ARMCR4_BLK_1K_MASK	0x200

static int brcmf_chip_socram_banksize(struct Core *core, UBYTE idx, ULONG *banksize)
{
    ULONG bankinfo;
    ULONG bankidx = (SOCRAM_MEMTYPE_RAM << SOCRAM_BANKIDX_MEMTYPE_SHIFT);

    bankidx |= idx;
    core->c_Chip->c_SDIO->Write32(core->c_BaseAddress + SOCRAMREGOFFS(bankidx), bankidx, core->c_Chip->c_SDIO);
    bankinfo = core->c_Chip->c_SDIO->Read32(core->c_BaseAddress + SOCRAMREGOFFS(bankinfo), core->c_Chip->c_SDIO);
    *banksize = (bankinfo & SOCRAM_BANKINFO_SZMASK) + 1;
    *banksize *= SOCRAM_BANKINFO_SZBASE;
    return !!(bankinfo & SOCRAM_BANKINFO_RETNTRAM_MASK);
}

static ULONG brcmf_chip_sysmem_ramsize(struct Core *sysmem)
{
    struct Chip *chip = sysmem->c_Chip;
    ULONG memsize = 0;
    ULONG coreinfo;
    ULONG idx;
    ULONG nb;
    ULONG banksize;

    if (!chip->IsCoreUp(chip, sysmem))
        chip->ResetCore(chip, sysmem, 0, 0, 0);

    coreinfo = chip->c_SDIO->Read32(sysmem->c_BaseAddress + SYSMEMREGOFFS(coreinfo), chip->c_SDIO);
    nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

    for (idx = 0; idx < nb; idx++) {
        brcmf_chip_socram_banksize(sysmem, idx, &banksize);
        memsize += banksize;
    }

    return memsize;
}

static void brcmf_chip_socram_ramsize(struct Core *sr, ULONG *ramsize, ULONG *srsize)
{
    struct Chip *chip = sr->c_Chip;
    ULONG coreinfo;
    ULONG nb, banksize, lss;
    int retent;
    ULONG i;

    *ramsize = 0;
    *srsize = 0;

    if (!chip->IsCoreUp(chip, sr))
        chip->ResetCore(chip, sr, 0, 0, 0);

    /* Get info for determining size */
    coreinfo = chip->c_SDIO->Read32(sr->c_BaseAddress + SOCRAMREGOFFS(coreinfo), chip->c_SDIO);
    nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;

    if ((sr->c_CoreREV <= 7) || (sr->c_CoreREV == 12)) {
        banksize = (coreinfo & SRCI_SRBSZ_MASK);
        lss = (coreinfo & SRCI_LSS_MASK) >> SRCI_LSS_SHIFT;
        if (lss != 0)
            nb--;
        *ramsize = nb * (1 << (banksize + SR_BSZ_BASE));
        if (lss != 0)
            *ramsize += (1 << ((lss - 1) + SR_BSZ_BASE));
    } else {
        /* length of SRAM Banks increased for corerev greater than 23 */
        if (sr->c_CoreREV >= 23) {
            nb = (coreinfo & (SRCI_SRNB_MASK | SRCI_SRNB_MASK_EXT))
                >> SRCI_SRNB_SHIFT;
        } else {
            nb = (coreinfo & SRCI_SRNB_MASK) >> SRCI_SRNB_SHIFT;
        }
        for (i = 0; i < nb; i++) {
            retent = brcmf_chip_socram_banksize(sr, i, &banksize);
            *ramsize += banksize;
            if (retent)
                *srsize += banksize;
        }
    }

    /* hardcoded save&restore memory sizes */
    switch (chip->c_ChipID) {
        case BRCM_CC_4334_CHIP_ID:
            if (chip->c_ChipREV < 2)
                *srsize = (32 * 1024);
            break;
        case BRCM_CC_43430_CHIP_ID:
        case CY_CC_43439_CHIP_ID:
            /* assume sr for now as we can not check
                * firmware sr capability at this point.
                */
            *srsize = (64 * 1024);
            break;
        default:
            break;
    }
}

static ULONG brcmf_chip_tcm_ramsize(struct Core *cr4)
{
    ULONG corecap;
    ULONG memsize = 0;
    ULONG nab;
    ULONG nbb;
    ULONG totb;
    ULONG bxinfo;
    ULONG blksize;
    ULONG idx;

    corecap = cr4->c_Chip->c_SDIO->Read32(cr4->c_BaseAddress + ARMCR4_CAP, cr4->c_Chip->c_SDIO);

    nab = (corecap & ARMCR4_TCBANB_MASK) >> ARMCR4_TCBANB_SHIFT;
    nbb = (corecap & ARMCR4_TCBBNB_MASK) >> ARMCR4_TCBBNB_SHIFT;
    totb = nab + nbb;

    for (idx = 0; idx < totb; idx++)
    {
        cr4->c_Chip->c_SDIO->Write32(cr4->c_BaseAddress + ARMCR4_BANKIDX, idx, cr4->c_Chip->c_SDIO);
        bxinfo = cr4->c_Chip->c_SDIO->Read32(cr4->c_BaseAddress + ARMCR4_BANKINFO, cr4->c_Chip->c_SDIO);
        blksize = ARMCR4_BSZ_MULT;
        if (bxinfo & ARMCR4_BLK_1K_MASK)
            blksize >>= 3;

        memsize += ((bxinfo & ARMCR4_BSZ_MASK) + 1) * blksize;
    }

    return memsize;
}

static ULONG brcmf_chip_tcm_rambase(struct Chip *ci)
{
    switch (ci->c_ChipID) {
        case BRCM_CC_4345_CHIP_ID:
        case BRCM_CC_43454_CHIP_ID:
            return 0x198000;
        case BRCM_CC_4335_CHIP_ID:
        case BRCM_CC_4339_CHIP_ID:
        case BRCM_CC_4350_CHIP_ID:
        case BRCM_CC_4354_CHIP_ID:
        case BRCM_CC_4356_CHIP_ID:
        case BRCM_CC_43567_CHIP_ID:
        case BRCM_CC_43569_CHIP_ID:
        case BRCM_CC_43570_CHIP_ID:
        case BRCM_CC_4358_CHIP_ID:
        case BRCM_CC_43602_CHIP_ID:
        case BRCM_CC_4371_CHIP_ID:
            return 0x180000;
        case BRCM_CC_43465_CHIP_ID:
        case BRCM_CC_43525_CHIP_ID:
        case BRCM_CC_4365_CHIP_ID:
        case BRCM_CC_4366_CHIP_ID:
        case BRCM_CC_43664_CHIP_ID:
        case BRCM_CC_43666_CHIP_ID:
            return 0x200000;
        case BRCM_CC_4355_CHIP_ID:
        case BRCM_CC_4359_CHIP_ID:
            return (ci->c_ChipREV < 9) ? 0x180000 : 0x160000;
        case BRCM_CC_4364_CHIP_ID:
        case CY_CC_4373_CHIP_ID:
            return 0x160000;
        case CY_CC_43752_CHIP_ID:
        case BRCM_CC_4377_CHIP_ID:
            return 0x170000;
        case BRCM_CC_4378_CHIP_ID:
            return 0x352000;
        case BRCM_CC_4387_CHIP_ID:
            return 0x740000;
        default:
            break;
    }
    return 0xffffffff;
}

int get_memory(struct Chip *chip)
{
    struct ExecBase *SysBase = chip->c_WiFiBase->w_SysBase;
    struct Core *mem = chip->GetCore(chip, BCMA_CORE_ARM_CR4);

    // CR4 has onboard RAM
    if (mem != NULL)
    {
        chip->c_RAMBase = brcmf_chip_tcm_rambase(chip);
        chip->c_RAMSize = brcmf_chip_tcm_ramsize(mem);
    }
    else
    {
        mem = chip->GetCore(chip, BCMA_CORE_SYS_MEM);
        if (mem) {
            chip->c_RAMSize = brcmf_chip_sysmem_ramsize(mem);
            chip->c_RAMBase = brcmf_chip_tcm_rambase(chip);
            if (chip->c_RAMBase == 0xffffffff) {
                bug("[WiFi] RAM base not provided with ARM CA7 core\n");
                return 0;
            }
        } else {
            mem = chip->GetCore(chip, BCMA_CORE_INTERNAL_MEM);
            if (!mem) {
                bug("[WiFi] No memory cores found\n");
                return 0;
            }
            
            brcmf_chip_socram_ramsize(mem, &chip->c_RAMSize, &chip->c_SRSize);
        }
    }

    D(bug("[WiFi] RAM Base: %08lx, Size: %08lx, SR: %08lx\n", chip->c_RAMBase, chip->c_RAMSize, chip->c_SRSize));
    
    return 1;
}


#define SDIOD_DRVSTR_KEY(chip, pmu)     (((unsigned int)(chip) << 16) | (pmu))

/* SDIO Pad drive strength to select value mappings */
struct sdiod_drive_str {
	UBYTE strength;	/* Pad Drive Strength in mA */
	UBYTE sel;		/* Chip-specific select value */
};

/* SDIO Drive Strength to sel value table for PMU Rev 11 (1.8V) */
static const struct sdiod_drive_str sdiod_drvstr_tab1_1v8[] = {
	{32, 0x6},
	{26, 0x7},
	{22, 0x4},
	{16, 0x5},
	{12, 0x2},
	{8, 0x3},
	{4, 0x0},
	{0, 0x1}
};

/* SDIO Drive Strength to sel value table for PMU Rev 13 (1.8v) */
static const struct sdiod_drive_str sdiod_drive_strength_tab5_1v8[] = {
	{6, 0x7},
	{5, 0x6},
	{4, 0x5},
	{3, 0x4},
	{2, 0x2},
	{1, 0x1},
	{0, 0x0}
};

/* SDIO Drive Strength to sel value table for PMU Rev 17 (1.8v) */
static const struct sdiod_drive_str sdiod_drvstr_tab6_1v8[] = {
	{3, 0x3},
	{2, 0x2},
	{1, 0x1},
	{0, 0x0} };

/* SDIO Drive Strength to sel value table for 43143 PMU Rev 17 (3.3V) */
static const struct sdiod_drive_str sdiod_drvstr_tab2_3v3[] = {
	{16, 0x7},
	{12, 0x5},
	{8,  0x3},
	{4,  0x1}
};

static void brcmf_sdio_drivestrengthinit(struct SDIO *sdio, struct Chip *ci, ULONG drivestrength)
{
    struct ExecBase *SysBase = sdio->s_SysBase;
    const struct sdiod_drive_str *str_tab = NULL;
    ULONG str_mask;
    ULONG str_shift;
    ULONG i;
    ULONG drivestrength_sel = 0;
    ULONG cc_data_temp;
    ULONG addr;

    if (!(ci->c_Caps & CC_CAP_PMU))
        return;

    switch (SDIOD_DRVSTR_KEY(ci->c_ChipID, ci->c_PMURev)) {
        case SDIOD_DRVSTR_KEY(BRCM_CC_4330_CHIP_ID, 12):
            str_tab = sdiod_drvstr_tab1_1v8;
            str_mask = 0x00003800;
            str_shift = 11;
            break;
        case SDIOD_DRVSTR_KEY(BRCM_CC_4334_CHIP_ID, 17):
            str_tab = sdiod_drvstr_tab6_1v8;
            str_mask = 0x00001800;
            str_shift = 11;
            break;
        case SDIOD_DRVSTR_KEY(BRCM_CC_43143_CHIP_ID, 17):
            /* note: 43143 does not support tristate */
            i = sizeof(sdiod_drvstr_tab2_3v3) / sizeof(sdiod_drvstr_tab2_3v3[0]) - 1;
            if (drivestrength >= sdiod_drvstr_tab2_3v3[i].strength) {
                str_tab = sdiod_drvstr_tab2_3v3;
                str_mask = 0x00000007;
                str_shift = 0;
            } else
                bug("[WiFi] Invalid SDIO Drive strength %ld\n", drivestrength);
            break;
        case SDIOD_DRVSTR_KEY(BRCM_CC_43362_CHIP_ID, 13):
            str_tab = sdiod_drive_strength_tab5_1v8;
            str_mask = 0x00003800;
            str_shift = 11;
            break;
        default:
            bug("[WiFi] No SDIO driver strength init needed for chip id %lx rev %ld pmurev %ld\n",
                    ci->c_ChipID, ci->c_ChipREV, ci->c_PMURev);
            break;
    }

    if (str_tab != NULL) {
        struct Core *pmu = brcmf_chip_get_pmu(ci);

        for (i = 0; str_tab[i].strength != 0; i++) {
            if (drivestrength >= str_tab[i].strength) {
                drivestrength_sel = str_tab[i].sel;
                break;
            }
        }
        addr = CORE_CC_REG(pmu->c_BaseAddress, chipcontrol_addr);
        sdio->Write32(addr, 1, sdio);
        cc_data_temp = sdio->Read32(addr, sdio);
        cc_data_temp &= ~str_mask;
        drivestrength_sel <<= str_shift;
        cc_data_temp |= drivestrength_sel;
        sdio->Write32(addr, cc_data_temp, sdio);

        D(bug("[WiFi] SDIO: %ld mA (req=%ld mA) drive strength selected, set to 0x%08lx\n",
                str_tab[i].strength, drivestrength, cc_data_temp));
    }
}

int chip_init(struct SDIO *sdio)
{
    struct ExecBase * SysBase = sdio->s_SysBase;
    struct WiFiBase * WiFiBase = sdio->s_WiFiBase;
    struct Chip *chip;

    // Get memory for the chip structure
    chip = AllocPooledClear(WiFiBase->w_MemPool, sizeof(struct Chip));
    
    D(bug("[WiFi] chip_init\n"));

    if (chip == NULL)
        return 0;

    // Initialize list of cores
    _NewList(&chip->c_Cores);

    chip->c_SDIO = sdio;
    chip->c_WiFiBase = WiFiBase;

    D(bug("[WiFi] Setting block sizes for backplane and radio functions\n"));

    /* Set blocksize for function 1 to 64 bytes */
    sdio->WriteByte(SD_FUNC_CIA, SDIO_FBR_ADDR(1, 0x10), 0x40, sdio);   // Function 1 - backplane
    sdio->WriteByte(SD_FUNC_CIA, SDIO_FBR_ADDR(1, 0x11), 0x00, sdio);

    /* Set blocksize for function 2 to 512 bytes */
    sdio->WriteByte(SD_FUNC_CIA, SDIO_FBR_ADDR(2, 0x10), 0x00, sdio);    // Function 2 - radio
    sdio->WriteByte(SD_FUNC_CIA, SDIO_FBR_ADDR(2, 0x11), 0x02, sdio);

    /* Enable backplane function */
    D(bug("[WiFi] Enabling function 1 (backplane)\n"));
    sdio->WriteByte(SD_FUNC_CIA, BUS_IOEN_REG, 1 << SD_FUNC_BAK, sdio);
    do {
        D(bug("[WiFi] Waiting...\n"));
    } while(0 == (sdio->ReadByte(SD_FUNC_CIA, BUS_IORDY_REG, sdio) & (1 << SD_FUNC_BAK)));
    D(bug("[WiFi] Backplane is up\n"));

    sdio->BackplaneAddr(SI_ENUM_BASE_DEFAULT, sdio);

    /* Force PLL off until the chip is attached */
    sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ, sdio);

    UBYTE tmp = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
    if (((tmp & ~SBSDIO_AVBITS) != (SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ))) {
        D(bug("[WiFi] Chip CLK CSR access error, wrote 0x%02lx, read 0x%02lx\n", SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ, tmp));
    }

    if (!sdio_buscoreprep(sdio))
    {
        FreePooled(WiFiBase->w_MemPool, chip, sizeof(struct Chip));
        return 0;
    }

    sdio->Write32(CORE_CC_REG(SI_ENUM_BASE_DEFAULT, gpiopullup), 0, sdio);
    sdio->Write32(CORE_CC_REG(SI_ENUM_BASE_DEFAULT, gpiopulldown), 0, sdio);

    ULONG id = sdio->Read32(CORE_CC_REG(SI_ENUM_BASE_DEFAULT, chipid), sdio);

    chip->c_ChipID = id & CID_ID_MASK;
    chip->c_ChipREV = (id & CID_REV_MASK) >> CID_REV_SHIFT;

    D(bug("[WiFi] Chip ID: %04lx rev %ld\n", chip->c_ChipID, chip->c_ChipREV));

    ULONG soci_type = (id & CID_TYPE_MASK) >> CID_TYPE_SHIFT;

    D(bug("[WiFi] SOCI type: %s\n", (ULONG)(soci_type == SOCI_SB ? "SB" : "AI")));

    // SOCI_AI - EROM contains information about available cores and their base addresses
    if (soci_type == SOCI_AI)
    {
        chip->IsCoreUp = brcm_chip_ai_iscoreup;
        chip->DisableCore = brcm_chip_ai_disablecore;
        chip->ResetCore = brcm_chip_ai_resetcore;
        brcm_chip_dmp_erom_scan(chip);
    }
    // SOCI_SB - force cores at fixed addresses. Actually it is most likely not really the
    // case on RaspberryPi
    else
    {
        D(bug("[WiFi] SB type SOCI not supported!\n"));
        FreePooled(WiFiBase->w_MemPool, chip, sizeof(struct Chip));
        return 0;
    }

    chip->GetCore = brcm_chip_get_core;

    // Check if all necessary cores were found
    if (!brcm_chip_cores_check(chip))
    {
        struct Core *core;
        while ((core = (struct Core *)RemHead((struct List *)&chip->c_Cores)))
        {
            FreePooled(WiFiBase->w_MemPool, core, sizeof(struct Core));
        }
        FreePooled(WiFiBase->w_MemPool, chip, sizeof(struct Chip));
        return 0;
    }

    chip->SetPassive(chip);

    get_memory(chip);

    chip_setup(chip);

    // Set drive strengh to 6mA
    brcmf_sdio_drivestrengthinit(sdio, chip, 6);

    /* Set card control so an SDIO card reset does a WLAN backplane reset */
    ULONG reg_val = sdio->ReadByte(SD_FUNC_CIA, SDIO_CCCR_BRCM_CARDCTRL, sdio);
    D(bug("[WiFi] CARDCTRL = %02lx, setting to %02lx\n", reg_val, reg_val | SDIO_CCCR_BRCM_CARDCTRL_WLANRESET));
    reg_val |= SDIO_CCCR_BRCM_CARDCTRL_WLANRESET;
    sdio->WriteByte(SD_FUNC_CIA, SDIO_CCCR_BRCM_CARDCTRL, reg_val, sdio);

    /* set PMUControl so a backplane reset does PMU state reload */
    ULONG reg_addr = CORE_CC_REG(brcmf_chip_get_pmu(chip)->c_BaseAddress, pmucontrol);
    reg_val = sdio->Read32(reg_addr, sdio);
    D(bug("[WiFi] PMU Control reg at %08lx = %08lx. Setting to ", reg_addr, reg_val));
    reg_val |= (BCMA_CC_PMU_CTL_RES_RELOAD << BCMA_CC_PMU_CTL_RES_SHIFT);
    D(bug("%08lx\n", reg_val));
    sdio->Write32(reg_addr, reg_val, sdio);

	/* Disable F2 to clear any intermediate frame state on the dongle */
    UBYTE reg = sdio->ReadByte(SD_FUNC_CIA, BUS_IOEN_REG, sdio);
    D(bug("[WiFi] Disabling function 2... BUS_IOEN_REG = %02lx\n", reg));
    reg &= ~(1 << SD_FUNC_RAD);
    sdio->WriteByte(SD_FUNC_CIA, BUS_IOEN_REG, reg, sdio);

    /* Done with backplane-dependent accesses, can drop clock... */
	sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, 0, sdio);
    sdio->s_ClkState = CLK_SDONLY;

    sdio->s_ALPOnly = TRUE;

    // Load firmware files from disk
    LoadFirmware(chip);

    D(bug("[WiFi] Firmware at %08lx\n", (ULONG)chip->c_FirmwareBase));
    D(bug("[WiFi] NVRAM at %08lx\n", (ULONG)chip->c_ConfigBase));
    D(bug("[WiFi] CLM at %08lx\n", (ULONG)chip->c_CLMBase));

    sdio->ClkCTRL(CLK_AVAIL, FALSE, sdio);

    if (chip->c_FirmwareBase && chip->c_FirmwareSize)
    {
        ULONG ram_base = chip->c_RAMBase;
        D(bug("[WiFi] Uploading firmware to %08lx...\n", ram_base));
        ULONG remaining = (ULONG)chip->c_FirmwareSize;
        UBYTE *sdio_bin = chip->c_FirmwareBase;
        ULONG pos;
    
        for (pos = 0; pos < (ULONG)chip->c_FirmwareSize; )
        {
            ULONG sz = remaining > 64 ? 64 : remaining;
            ULONG addr = sdio->BackplaneAddr(ram_base + pos, sdio);

            sz = (sz + 3) & ~3;

            sdio->Write(SD_FUNC_BAK, SB_32BIT_WIN + addr, &sdio_bin[pos], sz, sdio);
            if (sdio->IsError(sdio))
            {
                D(bug("[WiFi] Firmware write error!\n"));
            }
            pos += sz;
            remaining -= sz;
        }

        D(bug("[WiFi] wrote %ld bytes\n", pos));
    }

    if (chip->c_ConfigBase && chip->c_ConfigSize)
    {
        ULONG ram_base = chip->c_RAMBase + chip->c_RAMSize - chip->c_ConfigSize;
        D(bug("[WiFi] Uploading NVRAM to %08lx...\n", ram_base));
        ULONG remaining = (ULONG)chip->c_ConfigSize;
        UBYTE *nvram_bin = chip->c_ConfigBase;
        ULONG pos;

        for (pos = 0; pos < (ULONG)chip->c_ConfigSize; )
        {
            ULONG sz = remaining > 64 ? 64 : remaining;
            ULONG addr = sdio->BackplaneAddr(ram_base + pos, sdio);

            sz = (sz + 3) & ~3;

            sdio->Write(SD_FUNC_BAK, SB_32BIT_WIN + addr, &nvram_bin[pos], sz, sdio);
            if (sdio->IsError(sdio))
            {
                D(bug("[WiFi] NVRAM write error!\n"));
            }
            pos += sz;
            remaining -= sz;
        }
        D(bug("[WiFi] wrote %ld bytes\n", pos));
    }

    ULONG resetVector = LE32(*(ULONG*)chip->c_FirmwareBase);

    /* Take ARM out of reset */
    D(bug("[WiFi] Taking WiFi's ARM out of reset. Vector: %08lx\n", resetVector));
    chip->SetActive(chip, resetVector);

    sdio->ClkCTRL(CLK_SDONLY, FALSE, sdio);

    sdio->s_ALPOnly = FALSE;

    /* Make sure backplane clock is on, needed to generate F2 interrupt */
    sdio->ClkCTRL(CLK_AVAIL, FALSE, sdio);

    /* Force clocks on backplane to be sure F2 interrupt propagates */
    UBYTE saveclk = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio);
    if (!sdio->IsError(sdio)) {
        UBYTE bpreq = saveclk;
        bpreq |= chip->c_ChipID == CY_CC_43012_CHIP_ID ?
            SBSDIO_HT_AVAIL_REQ : SBSDIO_FORCE_HT;
        sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, bpreq, sdio);
    }
    if (sdio->IsError(sdio)) {
        bug("[WiFi] Failed to force clock for F2\n");
    }

    /* Enable function 2 (frame transfers) */
    struct Core *core = chip->GetCore(chip, BCMA_CORE_SDIO_DEV);
    sdio->Write32(core->c_BaseAddress + SD_REG(tosbmailboxdata), SDPCM_PROT_VERSION << SMB_DATA_VERSION_SHIFT, sdio);

    /* Enable RAD function */
    D(bug("[WiFi] Enabling function 2 (radio)\n"));
    reg = sdio->ReadByte(SD_FUNC_CIA, BUS_IOEN_REG, sdio);
    reg |= 1 << SD_FUNC_RAD;
    sdio->WriteByte(SD_FUNC_CIA, BUS_IOEN_REG, reg, sdio);
    ULONG timeout = 50;
    do {
        D(bug("[WiFi] Waiting...\n"));
        delay_us(10000, WiFiBase);
    } while(0 == (sdio->ReadByte(SD_FUNC_CIA, BUS_IORDY_REG, sdio) & (1 << SD_FUNC_RAD)) && --timeout);
    
    if (timeout == 0)
    {
        D(bug("[WiFi] Turning function 2 timed out!\n"));
    }
    else
        D(bug("[WiFi] Function 2 is up\n"));

    /* If F2 successfully enabled, set core and enable interrupts */
    if (!sdio->IsError(sdio))
    {
        UBYTE devctl;

        /* Set up the interrupt mask and enable interrupts */
        sdio->s_HostINTMask = HOSTINTMASK;
        sdio->Write32(core->c_BaseAddress + SD_REG(hostintmask), sdio->s_HostINTMask, sdio);

        switch (chip->c_ChipID) {
        case CY_CC_4373_CHIP_ID:
        case CY_CC_43752_CHIP_ID:
            D(bug("[WiFi] set F2 watermark to 0x%lx*4 bytes\n", CY_4373_F2_WATERMARK));
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, CY_4373_F2_WATERMARK, sdio);
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_MESBUSYCTRL, CY_4373_F1_MESBUSYCTRL, sdio);
            break;
        case CY_CC_43012_CHIP_ID:
            D(bug("[WiFi] set F2 watermark to 0x%lx*4 bytes\n", CY_43012_F2_WATERMARK));
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, CY_43012_F2_WATERMARK, sdio);
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_MESBUSYCTRL, CY_43012_MESBUSYCTRL, sdio);
            break;
        case BRCM_CC_4329_CHIP_ID:
        case BRCM_CC_4339_CHIP_ID:
            D(bug("[WiFi] set F2 watermark to 0x%lx*4 bytes\n", CY_4339_F2_WATERMARK));
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, CY_4339_F2_WATERMARK, sdio);
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_MESBUSYCTRL, CY_4339_MESBUSYCTRL, sdio);
            break;

        case BRCM_CC_4345_CHIP_ID:
            D(bug("[WiFi] set F2 watermark to 0x%lx*4 bytes\n", CY_43455_F2_WATERMARK));
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, CY_43455_F2_WATERMARK, sdio);
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_MESBUSYCTRL, CY_43455_MESBUSYCTRL, sdio);
            break;

        case BRCM_CC_4359_CHIP_ID:
        case BRCM_CC_4354_CHIP_ID:
        case BRCM_CC_4356_CHIP_ID:
            D(bug("[WiFi] set F2 watermark to 0x%lx*4 bytes\n", CY_435X_F2_WATERMARK));
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, CY_435X_F2_WATERMARK, sdio);
            devctl = sdio->ReadByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, sdio);
            devctl |= SBSDIO_DEVCTL_F2WM_ENAB;
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_DEVICE_CTL, devctl, sdio);
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_MESBUSYCTRL, CY_435X_F1_MESBUSYCTRL, sdio);
            break;
        default:
            sdio->WriteByte(SD_FUNC_BAK, SBSDIO_WATERMARK, DEFAULT_F2_WATERMARK, sdio);
            break;
        }
    }

    /* Restore previous clock setting */#
    sdio->WriteByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, saveclk, sdio);

    /* Allow full data communication using DPC from now on. */
	//brcmf_sdiod_change_state(bus->sdiodev, BRCMF_SDIOD_DATA);

#if 0

    UBYTE buf[64];
    for (int i=0; i < 64; i++) buf[i] = 0;

    sdio->GetIntStatus(sdio);

#if 1
    sdio->RecvPKT(buf, 8, sdio);
    D(bug("[WiFi] RecvPkt: \n"));


    for (int i=0; i < 64; i++)
    {
        if (i % 16 == 0) bug("[WiFi]");
        bug(" %02lx", buf[i]);
        if (i % 16 == 15) bug("\n");
    }

    sdio->RecvPKT(buf, 8, sdio);
    D(bug("[WiFi] RecvPkt: \n"));


    for (int i=0; i < 64; i++)
    {
        if (i % 16 == 0) bug("[WiFi]");
        bug(" %02lx", buf[i]);
        if (i % 16 == 15) bug("\n");
    }

#endif

    sdio->SendPKT(buf, 64, sdio);
    for (int i=0; i < 20; i++)
    {
        sdio->GetIntStatus(sdio);
        delay_us(100000, WiFiBase);
    }

    sdio->BackplaneAddr(core->c_BaseAddress, sdio);



    bug("[WiFi] CIA functions: %02lx\n", sdio->ReadByte(SD_FUNC_CIA, BUS_IORDY_REG, sdio));
    bug("[WiFi] Chipclk: %08lx\n", sdio->ReadByte(SD_FUNC_BAK, SBSDIO_FUNC1_CHIPCLKCSR, sdio));
#endif

    sdio->s_Chip = chip;
    
    return 1;
}
