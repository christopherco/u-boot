// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
 *
 * Copyright (C) 2013 Jon Nettleton <jon.nettleton@gmail.com>
 *
 * Based on SPL code from Solidrun tree, which is:
 * Author: Tungyi Lin <tungyilin1127@gmail.com>
 *
 * Derived from EDM_CF_IMX6 code by TechNexion,Inc
 * Ported to SolidRun microSOM by Rabeeh Khoury <rabeeh@solid-run.com>
 */

#include <asm/arch/clock.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/iomux.h>
#include <asm/arch/mx6-pins.h>
#include <asm/arch/mxc_hdmi.h>
#include <linux/errno.h>
#include <asm/gpio.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm/mach-imx/sata.h>
#include <asm/mach-imx/video.h>
#include <mmc.h>
#include <fsl_esdhc.h>
#include <malloc.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/arch/crm_regs.h>
#include <asm/io.h>
#include <asm/arch/sys_proto.h>
#include <spl.h>
#include <usb.h>
#include <usb/ehci-ci.h>
#include <div64.h>

DECLARE_GLOBAL_DATA_PTR;

/*#define UART_PAD_CTRL  (PAD_CTL_PUS_100K_UP |			\
	PAD_CTL_SPEED_MED | PAD_CTL_DSE_40ohm |			\
	PAD_CTL_SRE_FAST  | PAD_CTL_HYS)
*/

#define I2C_PAD_CTRL		0x1b0b0
#define SPI_PAD_CTRL		0x1b0b0
#define SD_PAD_CTRL		0x1b0b0
#define SD_CLK_PAD_CTRL		0x1b0f9
#define GPIO_DEFAULT_PAD_CTRL	0x1b0b0
#define GPIO_FAST_PAD_CTRL	0x0b0b1
#define GPIO_SLOW_PAD_CTRL	0x0b060
#define UART_PAD_CTRL		0x1b0b0
#define ENET_PAD_CTRL		0x1b0b0
#define USB_OTG_PAD_CTRL	0x1b0b0
#define USB_HOST_PAD_CTRL	0x1b0b0
#define CAN_PAD_CTRL		0x1b0b0
#define PWM_PAD_CTRL		0x1b0b0
#define TRACEIF_PAD_CTRL	0x1b0b0
#define WEIM_PAD_CTRL		0xb098
#define TOUCH_PAD_CTRL		0x1b0b0


#define IMX_IOMUX_V3_SETUP_MULTIPLE_PADS(X) \
		do { \
			imx_iomux_v3_setup_multiple_pads(X, ARRAY_SIZE(X)); \
		} while(0)

#define SETUP_MULTIPLE_PADS(Q, DL) \
		do { \
			switch(get_cpu_type()) { \
				case MXC_CPU_MX6D: \
				case MXC_CPU_MX6Q: \
				case MXC_CPU_MX6SOLO: \
				case MXC_CPU_MX6DL: \
					IMX_IOMUX_V3_SETUP_MULTIPLE_PADS(DL); break; \
			} \
		} while(0)
#define ETH_PHY_RESET	IMX_GPIO_NR(4, 15)
#define USB_H1_VBUS	IMX_GPIO_NR(1, 0)

void henry_on() {
	// Red LED pad to GPIO
	u32 val;
	__raw_writel(0x5,0x20E02E8);
	// GPIO (1,17) to output
 	val = __raw_readl(0x209C004);
	__raw_writel(val|0x30000,0x209C004);
	// GPIO (1, 17) off (LED ON)
	val = __raw_readl(0x209C000);
	__raw_writel(val&(~(0x30000)),0x209C000);
}

void henry_off() {
	// Red LED pad to GPIO
	u32 val;
	__raw_writel(0x5,0x20E02E8);
	// GPIO (1,17) to output
 	val = __raw_readl(0x209C004);
	__raw_writel(val|0x30000,0x209C004);
	// GPIO (1, 17) off (LED ON)
	val = __raw_readl(0x209C000);
	__raw_writel(val|(0x30000),0x209C000);
}
enum board_type {
	CUBOXI          = 0x00,
	HUMMINGBOARD    = 0x01,
	HUMMINGBOARD2   = 0x02,
	UNKNOWN         = 0x03,
};

#define MEM_STRIDE 0x4000000
static u32 get_ram_size_stride_test(u32 *base, u32 maxsize)
{
        volatile u32 *addr;
        u32          save[64];
        u32          cnt;
        u32          size;
        int          i = 0;

        /* First save the data */
        for (cnt = 0; cnt < maxsize; cnt += MEM_STRIDE) {
                addr = (volatile u32 *)((u32)base + cnt);       /* pointer arith! */
                sync ();
                save[i++] = *addr;
                sync ();
        }

        /* First write a signature */
        * (volatile u32 *)base = 0x12345678;
        for (size = MEM_STRIDE; size < maxsize; size += MEM_STRIDE) {
                * (volatile u32 *)((u32)base + size) = size;
                sync ();
                if (* (volatile u32 *)((u32)base) == size) {	/* We reached the overlapping address */
                        break;
                }
        }

        /* Restore the data */
        for (cnt = (maxsize - MEM_STRIDE); i > 0; cnt -= MEM_STRIDE) {
                addr = (volatile u32 *)((u32)base + cnt);       /* pointer arith! */
                sync ();
                *addr = save[i--];
                sync ();
        }

        return (size);
}

int dram_init(void)
{
	u32 max_size = imx_ddr_size();

	gd->ram_size = get_ram_size_stride_test((u32 *) CONFIG_SYS_SDRAM_BASE,
						(u32)max_size);

	return 0;
}

#if defined(CONFIG_MULTI_DTB_FIT) || defined(CONFIG_SPL_LOAD_FIT)
int board_fit_config_name_match(const char *name)
{
	return 0;
}
#endif

#define RST_LOC_GPIO	IMX_GPIO_NR(7, 13)
#define DEBUG0_GPIO	IMX_GPIO_NR(2, 0)
#define DEBUG1_GPIO	IMX_GPIO_NR(2, 1)
#define DEBUG2_GPIO	IMX_GPIO_NR(2, 2)

static void setup_iomux_aux(void) {

	iomux_v3_cfg_t mx6q_aux_pads[] = {
		MX6Q_PAD_GPIO_18__GPIO7_IO13
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* RST_LOC_GPIO */
		MX6Q_PAD_NANDF_D0__GPIO2_IO00
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG0_GPIO */
		MX6Q_PAD_NANDF_D1__GPIO2_IO01
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG1_GPIO */
		MX6Q_PAD_NANDF_D2__GPIO2_IO02
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG2_GPIO */
		MX6Q_PAD_GPIO_3__GPIO1_IO03
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PMIC_IRQn */
		MX6Q_PAD_DI0_PIN2__GPIO4_IO18
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* COMA_RSxxx_CAN#_2 */
		MX6Q_PAD_DI0_PIN3__GPIO4_IO19
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* COMA_RSxxx_CAN#_1 */
	};

	iomux_v3_cfg_t mx6dl_aux_pads[] = {
		MX6DL_PAD_GPIO_18__GPIO7_IO13
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* RST_LOC_GPIO */
		MX6DL_PAD_NANDF_D0__GPIO2_IO00
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG0_GPIO */
		MX6DL_PAD_NANDF_D1__GPIO2_IO01
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG1_GPIO */
		MX6DL_PAD_NANDF_D2__GPIO2_IO02
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DEBUG2_GPIO */
		MX6DL_PAD_GPIO_3__GPIO1_IO03
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PMIC_IRQn */
		MX6DL_PAD_DI0_PIN2__GPIO4_IO18
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* COMA_RSxxx_CAN#_2 */
		MX6DL_PAD_DI0_PIN3__GPIO4_IO19
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* COMA_RSxxx_CAN#_1 */
	};

	SETUP_MULTIPLE_PADS(mx6q_aux_pads, mx6dl_aux_pads);
}

static void board_pulse_local_reset(unsigned long usec)
{
	gpio_direction_output(RST_LOC_GPIO, 0);
	udelay(usec);
	gpio_set_value(RST_LOC_GPIO, 1);
	udelay(usec);
}

static int setup_pmic_voltages(void) {
	return 0;
}

void ldo_mode_set(int ldo_bypass)
{}


static void setup_iomux_uart(void)
{
        iomux_v3_cfg_t const uart2_pads[] = {
                IOMUX_PADS(PAD_SD3_DAT4__UART2_RX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL)),
                IOMUX_PADS(PAD_SD3_DAT5__UART2_TX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL)),
        };
	SETUP_IOMUX_PADS(uart2_pads);
}

#if defined(CONFIG_FSL_ESDHC)
/*
 * U-boot device node | Physical Port
 * ----------------------------------
 *  mmc0              |  SD2
 *  mmc1              |  eMMC
 */
#define CONFIG_SYS_FSL_USDHC_NUM 1
struct fsl_esdhc_cfg usdhc_cfg[CONFIG_SYS_FSL_USDHC_NUM];

int mmc_get_env_devno(void)
{
	return 0;
}

#define MMC2_CD_GPIO	IMX_GPIO_NR(1, 4)
#define MMC2_WP_GPIO	IMX_GPIO_NR(6, 11)

int board_mmc_getcd(struct mmc *mmc) {
	printf("mmc_getcd start\n");
	struct fsl_esdhc_cfg *cfg = (struct fsl_esdhc_cfg *)mmc->priv;
	int ret = 0;

	switch (cfg->esdhc_base) {
		case USDHC2_BASE_ADDR:
			ret = !gpio_get_value(MMC2_CD_GPIO);
			printf("ret = %d\n",ret);
			ret = 1;
			break;
		case USDHC4_BASE_ADDR:
			ret = 1; /* eMMC always present */
			break;
	}
	return ret;
}

static void init_mmc2(int idx) {

	iomux_v3_cfg_t const mx6dl_usdhc2_pads[] = {
		IOMUX_PADS(PAD_SD2_CLK__SD2_CLK | MUX_PAD_CTRL(SD_CLK_PAD_CTRL)),
		IOMUX_PADS(PAD_SD2_CMD__SD2_CMD | MUX_PAD_CTRL(SD_PAD_CTRL)),
		IOMUX_PADS(PAD_SD2_DAT0__SD2_DATA0 | MUX_PAD_CTRL(SD_PAD_CTRL)),
		IOMUX_PADS(PAD_SD2_DAT1__SD2_DATA1 | MUX_PAD_CTRL(SD_PAD_CTRL)),
		IOMUX_PADS(PAD_SD2_DAT2__SD2_DATA2 | MUX_PAD_CTRL(SD_PAD_CTRL)),
		IOMUX_PADS(PAD_SD2_DAT3__SD2_DATA3 | MUX_PAD_CTRL(SD_PAD_CTRL)),
		IOMUX_PADS(PAD_GPIO_4__GPIO1_IO04 | MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL)),	/* MMC2_CD_GPIO */
		IOMUX_PADS(PAD_NANDF_CS0__GPIO6_IO11 | MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL)),	/* MMC2_WP_GPIO */
	};

	SETUP_IOMUX_PADS(mx6dl_usdhc2_pads);

	usdhc_cfg[idx].esdhc_base = USDHC2_BASE_ADDR;
	usdhc_cfg[idx].sdhc_clk = mxc_get_clock(MXC_ESDHC2_CLK);
	usdhc_cfg[idx].max_bus_width = 4;
}

#if defined(CONFIG_SYS_USE_EMMC)

#define EMMC_RESET_GPIO		IMX_GPIO_NR(6, 8)

static void reset_emmc(void) {
	gpio_direction_output(EMMC_RESET_GPIO, 0);
	mdelay(1);
	gpio_direction_output(EMMC_RESET_GPIO, 1);
}

static void init_emmc(int idx) {

	iomux_v3_cfg_t const mx6q_usdhc4_pads[] = {
		MX6Q_PAD_SD4_CLK__SD4_CLK
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_CMD__SD4_CMD
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT0__SD4_DATA0
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT1__SD4_DATA1
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT2__SD4_DATA2
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT3__SD4_DATA3
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT4__SD4_DATA4
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT5__SD4_DATA5
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT6__SD4_DATA6
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_SD4_DAT7__SD4_DATA7
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6Q_PAD_NANDF_ALE__GPIO6_IO08
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* EMMC_RESET_GPIO */
	};

	iomux_v3_cfg_t const mx6dl_usdhc4_pads[] = {
		MX6DL_PAD_SD4_CLK__SD4_CLK
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_CMD__SD4_CMD
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT0__SD4_DATA0
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT1__SD4_DATA1
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT2__SD4_DATA2
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT3__SD4_DATA3
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT4__SD4_DATA4
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT5__SD4_DATA5
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT6__SD4_DATA6
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_SD4_DAT7__SD4_DATA7
			| MUX_PAD_CTRL(SD_PAD_CTRL),
		MX6DL_PAD_NANDF_ALE__GPIO6_IO08
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* EMMC_RESET_GPIO */
	};

	SETUP_MULTIPLE_PADS(mx6q_usdhc4_pads, mx6dl_usdhc4_pads);

	usdhc_cfg[idx].esdhc_base = USDHC4_BASE_ADDR;
	usdhc_cfg[idx].sdhc_clk = mxc_get_clock(MXC_ESDHC4_CLK);
	reset_emmc();
}
#else /* defined(CONFIG_SYS_USE_EMMC) */

static void init_emmc(int idx) {}

#endif /* defined(CONFIG_SYS_USE_EMMC) */

int board_mmc_init(bd_t *bis) {

	        // Red LED pad to GPIO
        u32 val;
        __raw_writel(0x5,0x20E02E8);
        // GPIO (1,17) to output
        val = __raw_readl(0x209C004);
        __raw_writel(val|0x30000,0x209C004);
        // GPIO (1, 17) off (LED ON)
        val = __raw_readl(0x209C000);
        __raw_writel(val&(~(0x30000)),0x209C000);

	printf("mmc_init\n");
	int i;
	for (i = 0; i < CONFIG_SYS_FSL_USDHC_NUM; i++) {
		switch (i) {
			case 0:
				printf("init_mmc2\n");
				init_mmc2(i);
				gd->arch.sdhc_clk = usdhc_cfg[i].sdhc_clk;
				break;
			case 1:
				printf("init_emmc\n");
				init_emmc(i);
				break;
			default:
				printf("Warning: you configured more USDHC controllers"
					"(%d) than supported by the board\n", i + 1);
				return 0;
		}

		if (fsl_esdhc_initialize(bis, &usdhc_cfg[i]))
			printf("Warning: failed to initialize mmc dev %d\n", i);
	}
	return 0;
}
#endif /* defined(CONFIG_FSL_ESDHC) */

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}

#define HW_COD0_GPIO		IMX_GPIO_NR(5, 6)
#define HW_COD1_GPIO		IMX_GPIO_NR(5, 5)
#define HW_COD2_GPIO		IMX_GPIO_NR(4, 31)
#define HW_COD3_GPIO		IMX_GPIO_NR(5, 7)
#define HW_COD4_GPIO		IMX_GPIO_NR(5, 8)
#define DDR_CONFIG0_GPIO	IMX_GPIO_NR(4, 30)
#define DDR_CONFIG1_GPIO	IMX_GPIO_NR(4, 29)
#define DDR_CONFIG2_GPIO	IMX_GPIO_NR(4, 28)
#define DDR_CONFIG3_GPIO	IMX_GPIO_NR(4, 27)
#define DDR_CONFIG4_GPIO	IMX_GPIO_NR(4, 26)

static void setup_iomux_strapping(void)
{
	iomux_v3_cfg_t const mx6q_hw_cod_gpios[] = {
		MX6Q_PAD_DISP0_DAT12__GPIO5_IO06
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD0_GPIO */
		MX6Q_PAD_DISP0_DAT11__GPIO5_IO05
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD1_GPIO */
		MX6Q_PAD_DISP0_DAT10__GPIO4_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD2_GPIO */
		MX6Q_PAD_DISP0_DAT13__GPIO5_IO07
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD3_GPIO */
		MX6Q_PAD_DISP0_DAT14__GPIO5_IO08
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD4_GPIO */
	};

	iomux_v3_cfg_t const mx6dl_hw_cod_gpios[] = {
		MX6DL_PAD_DISP0_DAT12__GPIO5_IO06
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD0_GPIO */
		MX6DL_PAD_DISP0_DAT11__GPIO5_IO05
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD1_GPIO */
		MX6DL_PAD_DISP0_DAT10__GPIO4_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD2_GPIO */
		MX6DL_PAD_DISP0_DAT13__GPIO5_IO07
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD3_GPIO */
		MX6DL_PAD_DISP0_DAT14__GPIO5_IO08
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* HW_COD4_GPIO */
	};

	iomux_v3_cfg_t const mx6q_ddr_config_gpios[] = {
		MX6Q_PAD_DISP0_DAT9__GPIO4_IO30
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG0_GPIO */
		MX6Q_PAD_DISP0_DAT8__GPIO4_IO29
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG1_GPIO */
		MX6Q_PAD_DISP0_DAT7__GPIO4_IO28
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG2_GPIO */
		MX6Q_PAD_DISP0_DAT6__GPIO4_IO27
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG3_GPIO */
		MX6Q_PAD_DISP0_DAT5__GPIO4_IO26
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG4_GPIO */
	};

	iomux_v3_cfg_t const mx6dl_ddr_config_gpios[] = {
		MX6DL_PAD_DISP0_DAT9__GPIO4_IO30
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG0_GPIO */
		MX6DL_PAD_DISP0_DAT8__GPIO4_IO29
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG1_GPIO */
		MX6DL_PAD_DISP0_DAT7__GPIO4_IO28
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG2_GPIO */
		MX6DL_PAD_DISP0_DAT6__GPIO4_IO27
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG3_GPIO */
		MX6DL_PAD_DISP0_DAT5__GPIO4_IO26
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* DDR_CONFIG4_GPIO */
	};

	SETUP_MULTIPLE_PADS(mx6q_hw_cod_gpios, mx6dl_hw_cod_gpios);
	SETUP_MULTIPLE_PADS(mx6q_ddr_config_gpios, mx6dl_ddr_config_gpios);
}

static void setup_iomux_traceif(void) {

	iomux_v3_cfg_t mx6q_traceif_pads[] = {
		MX6Q_PAD_GPIO_5__ARM_EVENTI
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_PIXCLK__ARM_EVENTO
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_VSYNC__ARM_TRACE00
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT4__ARM_TRACE01
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT5__ARM_TRACE02
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT6__ARM_TRACE03
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT7__ARM_TRACE04
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT10__ARM_TRACE07
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DAT11__ARM_TRACE08
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_DATA_EN__ARM_TRACE_CLK
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6Q_PAD_CSI0_MCLK__ARM_TRACE_CTL
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
	};

	iomux_v3_cfg_t mx6dl_traceif_pads[] = {
		MX6DL_PAD_GPIO_5__ARM_EVENTI
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_PIXCLK__ARM_EVENTO
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_VSYNC__ARM_TRACE00
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT4__ARM_TRACE01
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT5__ARM_TRACE02
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT6__ARM_TRACE03
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT7__ARM_TRACE04
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT10__ARM_TRACE07
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DAT11__ARM_TRACE08
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_DATA_EN__ARM_TRACE_CLK
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
		MX6DL_PAD_CSI0_MCLK__ARM_TRACE_CTL
			| MUX_PAD_CTRL(TRACEIF_PAD_CTRL),
	};

	SETUP_MULTIPLE_PADS(mx6q_traceif_pads, mx6dl_traceif_pads);
}

struct strapping_gpio {
	const char*	name;
	int		gpio;
};

struct strapping_gpio mem_strappings[] = {
	{ "DDR_CONFIG4", DDR_CONFIG4_GPIO },
	{ "DDR_CONFIG3", DDR_CONFIG3_GPIO },
	{ "DDR_CONFIG2", DDR_CONFIG2_GPIO },
	{ "DDR_CONFIG1", DDR_CONFIG1_GPIO },
	{ "DDR_CONFIG0", DDR_CONFIG0_GPIO },
	{ NULL,          0 },
};

struct strapping_gpio hw_cod_strappings[] = {
	{ "HW_CODE4", HW_COD4_GPIO },
	{ "HW_CODE3", HW_COD3_GPIO },
	{ "HW_CODE2", HW_COD2_GPIO },
	{ "HW_CODE1", HW_COD1_GPIO },
	{ "HW_CODE0", HW_COD0_GPIO },
	{ NULL,       0 },
};

static int read_strapping(const struct strapping_gpio* ptr)
{
	int val = 0;
	for (; ptr->name != NULL; ptr++) {
		gpio_direction_input(ptr->gpio);
		val <<= 1;
		val |= !gpio_get_value(ptr->gpio);
	}
	return val;
}

int get_mem_variant(void)
{
	return read_strapping(mem_strappings);
}

int get_hw_variant(void)
{
	return read_strapping(hw_cod_strappings);
}

static void setup_iomux_weim(void) {

	iomux_v3_cfg_t mx6q_weim_pads[] = {
		MX6Q_PAD_EIM_DA0__EIM_AD00
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA1__EIM_AD01
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA2__EIM_AD02
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA3__EIM_AD03
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA4__EIM_AD04
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA5__EIM_AD05
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA6__EIM_AD06
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA7__EIM_AD07
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA8__EIM_AD08
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA9__EIM_AD09
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA10__EIM_AD10
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA11__EIM_AD11
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA12__EIM_AD12
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA13__EIM_AD13
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA14__EIM_AD14
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6Q_PAD_EIM_DA15__EIM_AD15
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* ALE */
		MX6Q_PAD_EIM_LBA__EIM_LBA_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* CEn */
		MX6Q_PAD_EIM_CS0__EIM_CS0_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* WEn */
		MX6Q_PAD_EIM_RW__EIM_RW
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* LBn */
		MX6Q_PAD_EIM_EB0__EIM_EB0_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* OEn */
		MX6Q_PAD_EIM_OE__EIM_OE_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* UBn */
		MX6Q_PAD_EIM_EB1__EIM_EB1_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
	};

	iomux_v3_cfg_t mx6dl_weim_pads[] = {
		MX6DL_PAD_EIM_DA0__EIM_AD00
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA1__EIM_AD01
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA2__EIM_AD02
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA3__EIM_AD03
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA4__EIM_AD04
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA5__EIM_AD05
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA6__EIM_AD06
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA7__EIM_AD07
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA8__EIM_AD08
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA9__EIM_AD09
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA10__EIM_AD10
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA11__EIM_AD11
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA12__EIM_AD12
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA13__EIM_AD13
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA14__EIM_AD14
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		MX6DL_PAD_EIM_DA15__EIM_AD15
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* ALE */
		MX6DL_PAD_EIM_LBA__EIM_LBA_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* CEn */
		MX6DL_PAD_EIM_CS0__EIM_CS0_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* WEn */
		MX6DL_PAD_EIM_RW__EIM_RW
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* LBn */
		MX6DL_PAD_EIM_EB0__EIM_EB0_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* OEn */
		MX6DL_PAD_EIM_OE__EIM_OE_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
		/* UBn */
		MX6DL_PAD_EIM_EB1__EIM_EB1_B
			| MUX_PAD_CTRL(WEIM_PAD_CTRL),
	};

	SETUP_MULTIPLE_PADS(mx6q_weim_pads, mx6dl_weim_pads);
}


static void setup_iomux_unused(void) {

	iomux_v3_cfg_t mx6q_unused_pads[] = {
		MX6Q_PAD_EIM_CS1__GPIO2_IO24
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A16__GPIO2_IO22
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A17__GPIO2_IO21
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A18__GPIO2_IO20
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A19__GPIO2_IO19
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A20__GPIO2_IO18
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A21__GPIO2_IO17
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A22__GPIO2_IO16
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A23__GPIO6_IO06
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_A24__GPIO5_IO04
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6Q_PAD_EIM_WAIT__GPIO5_IO00
			| MUX_PAD_CTRL(GPIO_SLOW_PAD_CTRL),
		MX6Q_PAD_EIM_EB2__GPIO2_IO30
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),
		MX6Q_PAD_EIM_EB3__GPIO2_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),
	};

	iomux_v3_cfg_t mx6dl_unused_pads[] = {
		MX6DL_PAD_EIM_CS1__GPIO2_IO24
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A16__GPIO2_IO22
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A17__GPIO2_IO21
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A18__GPIO2_IO20
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A19__GPIO2_IO19
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A20__GPIO2_IO18
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A21__GPIO2_IO17
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A22__GPIO2_IO16
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A23__GPIO6_IO06
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_A24__GPIO5_IO04
			| MUX_PAD_CTRL(GPIO_FAST_PAD_CTRL),
		MX6DL_PAD_EIM_WAIT__GPIO5_IO00
			| MUX_PAD_CTRL(GPIO_SLOW_PAD_CTRL),
		MX6DL_PAD_EIM_EB2__GPIO2_IO30
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),
		MX6DL_PAD_EIM_EB3__GPIO2_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),
	};

	SETUP_MULTIPLE_PADS(mx6q_unused_pads, mx6dl_unused_pads);
}

struct weim_timings {
	u32	cs0gcr1;
	u32	cs0gcr2;
	u32	cs0rcr1;
	u32	cs0rcr2;
	u32	cs0wcr1;
	u32	cs0wcr2;
};

static void setup_weim_timings(void) {

	struct weim_timings *weim = (struct weim_timings *)(WEIM_BASE_ADDR);
	weim->cs0gcr1 = 0x00310089;
	weim->cs0gcr2 = 0x00001001;
	weim->cs0rcr1 = 0x09000000;
	weim->cs0rcr2 = 0x00000008;
	weim->cs0wcr1 = 0x09000000;
	weim->cs0wcr2 = 0x00000000;
}

#define PWM3_TO_LEDR_GPIO	IMX_GPIO_NR(6, 31)
#define PWM4_TO_LEDG_GPIO	IMX_GPIO_NR(6, 9)
#define PWM4_TO_LEDB_GPIO	IMX_GPIO_NR(6, 10)

struct pwm_backlight_data {
        int pwm_id;
        unsigned int max_brightness;
        unsigned int brightness;
        unsigned int pwm_period_ns;
};
#define MXC_PWM_CLK 17

static struct pwm_backlight_data *pwm;
static void * const pwm_bases[] = {
	(void *)PWM1_BASE_ADDR,
	(void *)PWM2_BASE_ADDR,
	(void *)PWM3_BASE_ADDR,
	(void *)PWM4_BASE_ADDR,
};

#define MX6_PWMCR                  0x00    /* PWM Control Register */
#define MX6_PWMSAR                 0x0C    /* PWM Sample Register */
#define MX6_PWMPR                  0x10    /* PWM Period Register */
#define MX6_PWMCR_PRESCALER(x)     (((x - 1) & 0xFFF) << 4)
#define MX6_PWMCR_STOPEN           (1 << 25)
#define MX6_PWMCR_DOZEEN           (1 << 24)
#define MX6_PWMCR_WAITEN           (1 << 23)
#define MX6_PWMCR_DBGEN            (1 << 22)
#define MX6_PWMCR_CLKSRC_IPG_HIGH  (2 << 16)
#define MX6_PWMCR_SWR              (1 << 3)
#define MX6_PWMCR_EN               (1 << 0)

static int pwm_enable(int pwm_id)
{
	unsigned long reg;

	//enable_pwm_clk(1,pwm_id);
	reg = readl(pwm_bases[pwm_id] + MX6_PWMCR);
	reg |= MX6_PWMCR_EN;

	writel(reg, pwm_bases[pwm_id] + MX6_PWMCR);

	return 0;
}

static void pwm_disable(int pwm_id)
{
	writel(MX6_PWMCR_SWR, pwm_bases[pwm_id] + MX6_PWMCR);
	//enable_pwm_clk(0,pwm_id);
}

static int pwm_config(int pwm_id, int duty_ns, int period_ns)
{
	unsigned long long c;
	unsigned long period_cycles, duty_cycles, prescale;
	u32 cr;

	if (period_ns == 0 || duty_ns > period_ns)
		return -EINVAL;

	c = mxc_get_clock(MXC_PWM_CLK);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	prescale = period_cycles / 0x10000 + 1;

	period_cycles /= prescale;
	c = (unsigned long long)period_cycles * duty_ns;
	do_div(c, period_ns);
	duty_cycles = c;

	/*
	 * according to imx pwm RM, the real period value should be
	 * PERIOD value in PWMPR plus 2.
	 */
	if (period_cycles > 2)
		period_cycles -= 2;
	else
		period_cycles = 0;

	writel(duty_cycles,   pwm_bases[pwm_id] + MX6_PWMSAR);
	writel(period_cycles, pwm_bases[pwm_id] + MX6_PWMPR);

	cr = MX6_PWMCR_PRESCALER(prescale) |
		MX6_PWMCR_STOPEN | MX6_PWMCR_DOZEEN |
		MX6_PWMCR_WAITEN | MX6_PWMCR_DBGEN |
		MX6_PWMCR_CLKSRC_IPG_HIGH;

	writel(cr, pwm_bases[pwm_id] + MX6_PWMCR);

	return 0;
}

static int txbr2_imx6_pwm_backlight_update_status(void)
{
	int brightness = pwm->brightness;
	int max = pwm->max_brightness;

	if (brightness == 0) {
			pwm_config(pwm->pwm_id, 0, pwm->pwm_period_ns);
			pwm_disable(pwm->pwm_id);
	} else {
			pwm_config(pwm->pwm_id,
					brightness * pwm->pwm_period_ns / max, pwm->pwm_period_ns);
			pwm_enable(pwm->pwm_id);
	}
	return 0;
}


int txbr2_imx6_setup_pwm_backlight(struct pwm_backlight_data *pd)
{
	pwm = pd;

	if(pwm->pwm_id >= ARRAY_SIZE(pwm_bases))
		return -EINVAL;

	//enable_pwm_clk(1,pwm->pwm_id);

	txbr2_imx6_pwm_backlight_update_status();

	return 0;
}
static void setup_iomux_rgb_led(void) {

	iomux_v3_cfg_t const mx6q_rgb_led_pads[] = {
		MX6Q_PAD_SD1_DAT1__PWM3_OUT
			| MUX_PAD_CTRL(PWM_PAD_CTRL),	/* PWM3 */
		MX6Q_PAD_SD1_CMD__PWM4_OUT
			| MUX_PAD_CTRL(PWM_PAD_CTRL),	/* PWM4 */
		MX6Q_PAD_EIM_BCLK__GPIO6_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM3_TO_LEDR */
		MX6Q_PAD_NANDF_WP_B__GPIO6_IO09
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM4_TO_LEDG */
		MX6Q_PAD_NANDF_RB0__GPIO6_IO10
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM4_TO_LEDB */
	};

	iomux_v3_cfg_t const mx6dl_rgb_led_pads[] = {
		MX6DL_PAD_SD1_DAT1__PWM3_OUT
			| MUX_PAD_CTRL(PWM_PAD_CTRL),	/* PWM3 */
		MX6DL_PAD_SD1_CMD__PWM4_OUT
			| MUX_PAD_CTRL(PWM_PAD_CTRL),	/* PWM4 */
		MX6DL_PAD_EIM_BCLK__GPIO6_IO31
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM3_TO_LEDR */
		MX6DL_PAD_NANDF_WP_B__GPIO6_IO09
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM4_TO_LEDG */
		MX6DL_PAD_NANDF_RB0__GPIO6_IO10
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* PWM4_TO_LEDB */
	};

	SETUP_MULTIPLE_PADS(mx6q_rgb_led_pads, mx6dl_rgb_led_pads);
}

static void setup_rgb_led(void)
{
	struct pwm_backlight_data pwm;

	setup_iomux_rgb_led();

	/* red */
	pwm.pwm_id = 2;
	pwm.max_brightness = 255;
	pwm.brightness = 255;
	pwm.pwm_period_ns = 50000;
	txbr2_imx6_setup_pwm_backlight(&pwm);

	/* green and blue */
	pwm.pwm_id = 3;
	pwm.max_brightness = 255;
	pwm.brightness = 0;
	pwm.pwm_period_ns = 50000;
	txbr2_imx6_setup_pwm_backlight(&pwm);

	/* enable blue */
	gpio_direction_output(PWM4_TO_LEDB_GPIO, 0);
}

int board_early_init_f(void)
{
	//setup_iomux_aux();
	setup_iomux_uart();
//	__raw_writel(0xF,0x2020040);
	//setup_iomux_strapping();
	//setup_iomux_traceif();
	//setup_iomux_unused();
//	setup_iomux_weim();

/*
#ifdef CONFIG_PCIE_IMX
	setup_iomux_pcie();
#endif

#ifdef CONFIG_CMD_SATA
	setup_sata();
#endif

#ifdef CONFIG_USB_EHCI_MX6
	setup_usb();
#endif
*/
	return 0;
}

int board_init(void)
{
	//ccgr_init();
//	board_early_init_f();
//	setup_rgb_led();
	int ret = 0;

	/* address of boot parameters */
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

#ifdef CONFIG_VIDEO_IPUV3
	ret = setup_display();
#endif

	return ret;
}

enum {
	HW_T7BR2B     = 0,
	HW_T7BR2HP    = 1,
	HW_T7BR2S     = 2,
	HW_T7BR2B_12V = 3,
	HW_T12BR2B    = 4,
	HW_T12BR2HP   = 5,
	HW_T12BR2S    = 6,
};
enum {
	MEM_2GiBx64   = 0,
	MEM_1GiBx32   = 1,
};

int checkboard(void) {

	puts("Board: ");
	switch(get_hw_variant()) {
		case HW_T7BR2B:     puts("T7BR2B ");     break;
		case HW_T7BR2HP:    puts("T7BR2HP ");    break;
		case HW_T7BR2S:     puts("T7BR2S ");     break;
		case HW_T7BR2B_12V: puts("T7BR2B-12V "); break;
		case HW_T12BR2B:    puts("T12BR2B ");    break;
		case HW_T12BR2HP:   puts("T12BR2HP ");   break;
		case HW_T12BR2S:    puts("T12BR2S ");    break;
		default:
			puts("Unknown ");
			break;
	}
	puts("i.MX6");
	switch(get_cpu_type()) {
		case MXC_CPU_MX6SOLO: puts("S");  break;
		case MXC_CPU_MX6DL:   puts("DL"); break;
		case MXC_CPU_MX6D:    puts("D");  break;
		case MXC_CPU_MX6Q:    puts("Q");  break;
		default:              puts("?");  break;
	}
	puts(", ");
	switch(get_mem_variant()) {
		case MEM_2GiBx64: puts("2GiB x64"); break;
		case MEM_1GiBx32: puts("1GiB x32"); break;
		default:
			puts("?");  break;
	}
	puts("\n");
	return 0;
}

int board_late_init(void)
{
	return 0;
}

#ifdef CONFIG_SPL_BUILD
#include <asm/arch/mx6-ddr.h>
static const struct mx6dq_iomux_ddr_regs mx6q_ddr_ioregs = {
	.dram_sdclk_0 =  0x00020030,
	.dram_sdclk_1 =  0x00020030,
	.dram_cas =  0x00020030,
	.dram_ras =  0x00020030,
	.dram_reset =  0x000c0030,
	.dram_sdcke0 =  0x00003000,
	.dram_sdcke1 =  0x00003000,
	.dram_sdba2 =  0x00000000,
	.dram_sdodt0 =  0x00003030,
	.dram_sdodt1 =  0x00003030,
	.dram_sdqs0 =  0x00000030,
	.dram_sdqs1 =  0x00000030,
	.dram_sdqs2 =  0x00000030,
	.dram_sdqs3 =  0x00000030,
	.dram_sdqs4 =  0x00000030,
	.dram_sdqs5 =  0x00000030,
	.dram_sdqs6 =  0x00000030,
	.dram_sdqs7 =  0x00000030,
	.dram_dqm0 =  0x00020030,
	.dram_dqm1 =  0x00020030,
	.dram_dqm2 =  0x00020030,
	.dram_dqm3 =  0x00020030,
	.dram_dqm4 =  0x00020030,
	.dram_dqm5 =  0x00020030,
	.dram_dqm6 =  0x00020030,
	.dram_dqm7 =  0x00020030,
};

static const struct mx6sdl_iomux_ddr_regs mx6dl_ddr_ioregs = {
	.dram_sdclk_0 = 0x00000028,
	.dram_sdclk_1 = 0x00000028,
	.dram_cas =	0x00000028,
	.dram_ras =	0x00000028,
	.dram_reset =	0x000c0028,
	.dram_sdcke0 =	0x00003000,
	.dram_sdcke1 =	0x00003000,
	.dram_sdba2 =	0x00000000,
	.dram_sdodt0 =	0x00003030,
	.dram_sdodt1 =	0x00003030,
	.dram_sdqs0 =	0x00000028,
	.dram_sdqs1 =	0x00000028,
	.dram_sdqs2 =	0x00000028,
	.dram_sdqs3 =	0x00000028,
	.dram_sdqs4 =	0x00000028,
	.dram_sdqs5 =	0x00000028,
	.dram_sdqs6 =	0x00000028,
	.dram_sdqs7 =	0x00000028,
	.dram_dqm0 =	0x00000028,
	.dram_dqm1 =	0x00000028,
	.dram_dqm2 =	0x00000028,
	.dram_dqm3 =	0x00000028,
	.dram_dqm4 =	0x00000028,
	.dram_dqm5 =	0x00000028,
	.dram_dqm6 =	0x00000028,
	.dram_dqm7 =	0x00000028,
};

static const struct mx6dq_iomux_grp_regs mx6q_grp_ioregs = {
	.grp_ddr_type =  0x000C0000,
	.grp_ddrmode_ctl =  0x00020000,
	.grp_ddrpke =  0x00000000,
	.grp_addds =  0x00000030,
	.grp_ctlds =  0x00000030,
	.grp_ddrmode =  0x00020000,
	.grp_b0ds =  0x00000030,
	.grp_b1ds =  0x00000030,
	.grp_b2ds =  0x00000030,
	.grp_b3ds =  0x00000030,
	.grp_b4ds =  0x00000030,
	.grp_b5ds =  0x00000030,
	.grp_b6ds =  0x00000030,
	.grp_b7ds =  0x00000030,
};

static const struct mx6sdl_iomux_grp_regs mx6sdl_grp_ioregs = {
	.grp_ddr_type = 0x000c0000,
	.grp_ddrmode_ctl = 0x00020000,
	.grp_ddrpke = 0x00000000,
	.grp_addds = 0x00000028,
	.grp_ctlds = 0x00000028,
	.grp_ddrmode = 0x00020000,
	.grp_b0ds = 0x00000028,
	.grp_b1ds = 0x00000028,
	.grp_b2ds = 0x00000028,
	.grp_b3ds = 0x00000028,
	.grp_b4ds = 0x00000028,
	.grp_b5ds = 0x00000028,
	.grp_b6ds = 0x00000028,
	.grp_b7ds = 0x00000028,
};

/* microSOM with Dual processor and 1GB memory */
static const struct mx6_mmdc_calibration mx6q_1g_mmcd_calib = {
	.p0_mpwldectrl0 =  0x00000000,
	.p0_mpwldectrl1 =  0x00000000,
	.p1_mpwldectrl0 =  0x00000000,
	.p1_mpwldectrl1 =  0x00000000,
	.p0_mpdgctrl0 =    0x0314031c,
	.p0_mpdgctrl1 =    0x023e0304,
	.p1_mpdgctrl0 =    0x03240330,
	.p1_mpdgctrl1 =    0x03180260,
	.p0_mprddlctl =    0x3630323c,
	.p1_mprddlctl =    0x3436283a,
	.p0_mpwrdlctl =    0x36344038,
	.p1_mpwrdlctl =    0x422a423c,
};

/* microSOM with Quad processor and 2GB memory */
static const struct mx6_mmdc_calibration mx6q_2g_mmcd_calib = {
	.p0_mpwldectrl0 =  0x00000000,
	.p0_mpwldectrl1 =  0x00000000,
	.p1_mpwldectrl0 =  0x00000000,
	.p1_mpwldectrl1 =  0x00000000,
	.p0_mpdgctrl0 =    0x0314031c,
	.p0_mpdgctrl1 =    0x023e0304,
	.p1_mpdgctrl0 =    0x03240330,
	.p1_mpdgctrl1 =    0x03180260,
	.p0_mprddlctl =    0x3630323c,
	.p1_mprddlctl =    0x3436283a,
	.p0_mpwrdlctl =    0x36344038,
	.p1_mpwrdlctl =    0x422a423c,
};

/* microSOM with Solo processor and 512MB memory */
static const struct mx6_mmdc_calibration mx6dl_512m_mmcd_calib = {
	.p0_mpwldectrl0 = 0x0045004D,
	.p0_mpwldectrl1 = 0x003A0047,
	.p0_mpdgctrl0 =   0x023C0224,
	.p0_mpdgctrl1 =   0x02000220,
	.p0_mprddlctl =   0x44444846,
	.p0_mpwrdlctl =   0x32343032,
};

/* microSOM with Dual lite processor and 1GB memory */
static const struct mx6_mmdc_calibration mx6dl_1g_mmcd_calib = {
	.p0_mpwldectrl0 =  0x0045004D,
	.p0_mpwldectrl1 =  0x003A0047,
	.p1_mpwldectrl0 =  0x001F001F,
	.p1_mpwldectrl1 =  0x00210035,
	.p0_mpdgctrl0 =    0x023C0224,
	.p0_mpdgctrl1 =    0x02000220,
	.p1_mpdgctrl0 =    0x02200220,
	.p1_mpdgctrl1 =    0x02040208,
	.p0_mprddlctl =    0x44444846,
	.p1_mprddlctl =    0x4042463C,
	.p0_mpwrdlctl =    0x32343032,
	.p1_mpwrdlctl =    0x36363430,
};


static struct mx6_ddr3_cfg mem_ddr_2g = {
	.mem_speed = 1600,
	.density   = 2,
	.width     = 16,
	.banks     = 8,
	.rowaddr   = 15,
	.coladdr   = 10,
	.pagesz    = 2,
	.trcd      = 1375,
	.trcmin    = 4875,
	.trasmin   = 3500,
};
/*
static struct mx6_ddr3_cfg mem_ddr_2g = {
	.mem_speed = 1600,
	.density   = 2,
	.width     = 16,
	.banks     = 8,
	.rowaddr   = 14,
	.coladdr   = 10,
	.pagesz    = 2,
	.trcd      = 1375,
	.trcmin    = 4875,
	.trasmin   = 3500,
};
*/

static struct mx6_ddr3_cfg mem_ddr_4g = {
	.mem_speed = 1600,
	.density = 4,
	.width = 16,
	.banks = 8,
	.rowaddr = 16,
	.coladdr = 10,
	.pagesz = 2,
	.trcd = 1375,
	.trcmin = 4875,
	.trasmin = 3500,
};

static void ccgr_init(void)
{
	struct mxc_ccm_reg *ccm = (struct mxc_ccm_reg *)CCM_BASE_ADDR;

	writel(0xFFFFFFFF, &ccm->CCGR0);
	writel(0xFFFFFFFF, &ccm->CCGR1);
	writel(0xFFFFFFFF, &ccm->CCGR2);
	writel(0xFFFFFFFF, &ccm->CCGR3);
	writel(0xFFFFFFFF, &ccm->CCGR4);
	writel(0xFFFFFFFF, &ccm->CCGR5);
	writel(0xFFFFFFFF, &ccm->CCGR6);
}

static void spl_dram_init(int width)
{
	struct mx6_ddr_sysinfo sysinfo = {
		/* width of data bus: 0=16, 1=32, 2=64 */
		.dsize = width / 32,
		/* config for full 4GB range so that get_mem_size() works */
		.cs_density = 32,	/* 32Gb per CS */
		.ncs = 1,		/* single chip select */
		.cs1_mirror = 0,
		.rtt_wr = 1 /*DDR3_RTT_60_OHM*/,	/* RTT_Wr = RZQ/4 */
		.rtt_nom = 1 /*DDR3_RTT_60_OHM*/,	/* RTT_Nom = RZQ/4 */
		.walat = 1,	/* Write additional latency */
		.ralat = 5,	/* Read additional latency */
		.mif3_mode = 3,	/* Command prediction working mode */
		.bi_on = 1,	/* Bank interleaving enabled */
		.sde_to_rst = 0x10,	/* 14 cycles, 200us (JEDEC default) */
		.rst_to_cke = 0x23,	/* 33 cycles, 500us (JEDEC default) */
		.ddr_type = DDR_TYPE_DDR3,
		.refsel = 1,	/* Refresh cycles at 32KHz */
		.refr = 7,	/* 8 refresh commands per refresh cycle */
	};

//	if (is_mx6dq())
//		mx6dq_dram_iocfg(width, &mx6q_ddr_ioregs, &mx6q_grp_ioregs);
//	else
		mx6sdl_dram_iocfg(width, &mx6dl_ddr_ioregs, &mx6sdl_grp_ioregs);

//	if (is_cpu_type(MXC_CPU_MX6D))
//		mx6_dram_cfg(&sysinfo, &mx6q_1g_mmcd_calib, &mem_ddr_2g);
//	else if (is_cpu_type(MXC_CPU_MX6Q))
//		mx6_dram_cfg(&sysinfo, &mx6q_2g_mmcd_calib, &mem_ddr_4g);
//	else if (is_cpu_type(MXC_CPU_MX6DL))
		mx6_dram_cfg(&sysinfo, &mx6dl_1g_mmcd_calib, &mem_ddr_2g);
//	else if (is_cpu_type(MXC_CPU_MX6SOLO))
//		mx6_dram_cfg(&sysinfo, &mx6dl_512m_mmcd_calib, &mem_ddr_2g);
}

#define TEST_GPIO	IMX_GPIO_NR(6, 18)
void toggle_gpio(){
	// Set up PAD
	iomux_v3_cfg_t mx6dl_aux_pads[] = {
		MX6DL_PAD_SD3_DAT6__GPIO6_IO18
			| MUX_PAD_CTRL(GPIO_DEFAULT_PAD_CTRL),	/* TEST_GPIO */
	};
	IMX_IOMUX_V3_SETUP_MULTIPLE_PADS(mx6dl_aux_pads);
	// Set Direction and value
	gpio_direction_output(TEST_GPIO, 0);
	udelay(100);
	gpio_set_value(TEST_GPIO, 1);
	udelay(100);
	gpio_set_value(TEST_GPIO, 0);
	udelay(100);
	gpio_set_value(TEST_GPIO, 1);
}


void board_init_f(ulong dummy)
{
	/* setup AIPS and disable watchdog */
	arch_cpu_init();

	ccgr_init();	
//toggle_gpio();
	gpr_init();

	/* iomux and setup of i2c */
	board_early_init_f();
//	setup_rgb_led();
	/* setup GP timer */
	timer_init();

	/* UART clocks enabled and gd valid - init serial console */
	preloader_console_init();	

	/* DDR initialization */
	//if (is_cpu_type(MXC_CPU_MX6SOLO))
		spl_dram_init(32);
//	else
//		spl_dram_init(64);

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

	/* load/boot image from boot device */
	board_init_r(NULL, 0);
}
#endif
