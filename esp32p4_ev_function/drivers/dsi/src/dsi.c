/* SPDX-License-Identifier: MIT */
/*
 * MIPI-DSI host + EK79007 (Function EV) — MMIO bring-up (IDF-like, no VPG).
 *
 * Aligned to esp-bsp Function EV + esp_lcd_ek79007 + esp_lcd DPI path:
 *   - DPI expect 52 MHz (vendor), real PLL_F240M/5 = 48 MHz (IDF divider)
 *   - porches 10/160/160 + 1/23/12 (EK79007_1024_600_PANEL_60HZ_CONFIG)
 *   - 2-lane @ 1000 Mbps (BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS)
 *   - RGB565 (BSP default)
 *   - LP blanking + AUTO clock lane (dpi_panel_init); no host VPG
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "dsi.h"
#include "board_lcd.h"

#include "board_console.h"

#define HOST_BASE		ULMK_BOARD_DSI_HOST_BASE
#define BRG_BASE		ULMK_BOARD_DSI_BRG_BASE

#define HP_CLKRST		0x500E6000u
#define SOC_CLK_CTRL1		(HP_CLKRST + 0x18u)
#define REF_CLK_CTRL1		(HP_CLKRST + 0x28u)
#define REF_CLK_CTRL2		(HP_CLKRST + 0x2cu)
#define PERI_CLK_CTRL02		(HP_CLKRST + 0x38u)
#define PERI_CLK_CTRL03		(HP_CLKRST + 0x3cu)
#define HP_RST_EN0		(HP_CLKRST + 0xc0u)

#define DSI_SYS_CLK_EN		(1u << 12)
#define RST_EN_DSI_BRG		(1u << 26)
#define REF_240M_CLK_EN		(1u << 30)
#define REF_160M_CLK_EN		(1u << 0)
#define REF_20M_CLK_EN		(1u << 8)

#define DPHY_CFG_CLK_EN		(1u << 0)
#define DPHY_PLL_REFCLK_EN	(1u << 1)
#define DPICLK_SRC_S		5
#define DPICLK_EN		(1u << 7)
#define DPICLK_DIV_S		8
#define PLL_REFCLK_SRC_S	16
#define PLL_REFCLK_DIV_S	19

#define PMU_BASE		0x50115000u
#define LDO_VO3_CTRL		(PMU_BASE + 0x1c0u)
#define LDO_VO3_ANA		(PMU_BASE + 0x1c4u)
#define LDO_FORCE_TIEH_SEL	(1u << 7)
#define LDO_XPD			(1u << 8)
#define LDO_TIEH		(1u << 14)
#define LDO_ANA_MUL_S		23
#define LDO_ANA_EN_VDET		(1u << 26)
#define LDO_ANA_DREF_S		28

#define H_PWR_UP		0x04u
#define H_CLKMGR		0x08u
#define H_DPI_VCID		0x0cu
#define H_DPI_COLOR		0x10u
#define H_PCKHDL		0x2cu
#define H_MODE_CFG		0x34u
#define H_VID_MODE		0x38u
#define H_VID_PKT		0x3cu
#define H_VID_CHUNKS		0x40u
#define H_VID_NULL		0x44u
#define H_VID_HSA		0x48u
#define H_VID_HBP		0x4cu
#define H_VID_HLINE		0x50u
#define H_VID_VSA		0x54u
#define H_VID_VBP		0x58u
#define H_VID_VFP		0x5cu
#define H_VID_VACT		0x60u
#define H_CMD_MODE		0x68u
#define H_GEN_HDR		0x6cu
#define H_CMD_PKT_ST		0x74u
#define H_LPCLK_CTRL		0x94u
#define H_PHY_TMR_LPCLK		0x98u
#define H_PHY_TMR		0x9cu
#define H_PHY_RSTZ		0xa0u
#define H_PHY_IF_CFG		0xa4u
#define H_PHY_STATUS		0xb0u
#define H_INT_ST0		0xbcu
#define H_INT_ST1		0xc0u
#define H_DPI_COLOR_ACT		0x110u
#define H_VID_MODE_ACT		0x138u
#define H_PHY_TST0		0xb4u
#define H_PHY_TST1		0xb8u

#define BRG_EN			0x04u
#define BRG_DPI_V_CFG0		0x30u
#define BRG_DPI_V_CFG1		0x34u
#define BRG_DPI_H_CFG0		0x38u
#define BRG_DPI_H_CFG1		0x3cu
#define BRG_DPI_MISC		0x40u
#define BRG_DPI_UPD		0x44u
#define BRG_HOST_CTRL		0x80u

/* EK79007_1024_600_PANEL_60HZ_CONFIG_CF + BSP lane rate */
#define PANEL_H			1024u
#define PANEL_V			600u
#define HSW			10u
#define HBP			160u
#define HFP			160u
#define VSW			1u
#define VBP			23u
#define VFP			12u

#define LANE_NUM		2u
#define LANE_MBPS		1000u

/* PHY PLL: ref = PLL_F20M, N=1, M=50 → 1000 Mbps; [1000,1050)=0x2A */
#define PLL_REF_MHZ		20u
#define PLL_N			1u
#define PLL_M			(LANE_MBPS * PLL_N / PLL_REF_MHZ)
#define PLL_HS_FREQ_SEL		0x2Au
/* Vendor asks 52 MHz; IDF divider from PLL_F240M → real 48 MHz */
#define DPI_EXPECT_MHZ		52u
#define DPI_REAL_MHZ		48u
#define DPI_SRC_SEL		1u /* PLL_F240M */
#define DPI_DIV			5u /* round(240/52)=5 → 48 MHz */

#define PHY_SHUTDOWNZ		(1u << 0)
#define PHY_RSTZ		(1u << 1)
#define PHY_ENABLECLK		(1u << 2)
#define PHY_FORCEPLL		(1u << 3)

#define DT_DCS_SHORT_0		0x05u
#define DT_DCS_SHORT_1		0x15u

#define CMD_FIFO_EMPTY		(1u << 0)
#define CMD_FIFO_FULL		(1u << 1)

static int g_dsi_ok;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static inline void host_wr(uint32_t off, uint32_t v)
{
	wr(HOST_BASE + off, v);
}

static inline uint32_t host_rd(uint32_t off)
{
	return rd(HOST_BASE + off);
}

static inline void brg_wr(uint32_t off, uint32_t v)
{
	wr(BRG_BASE + off, v);
}

static void settle(uint32_t n)
{
	volatile uint32_t i;

	for (i = 0u; i < n; i++)
		;
}

static void ldo_vo3_mipi_2v5(void)
{
	uint32_t ctrl;
	uint32_t ana;

	ana = (3u << LDO_ANA_MUL_S) | LDO_ANA_EN_VDET | (13u << LDO_ANA_DREF_S);
	wr(LDO_VO3_ANA, ana);

	ctrl = rd(LDO_VO3_CTRL);
	ctrl |= LDO_FORCE_TIEH_SEL | LDO_XPD;
	ctrl &= ~LDO_TIEH;
	wr(LDO_VO3_CTRL, ctrl);
	settle(40000u);
}

static void dsi_clocks_enable(void)
{
	uint32_t v;

	v = rd(SOC_CLK_CTRL1);
	wr(SOC_CLK_CTRL1, v | DSI_SYS_CLK_EN);

	v = rd(REF_CLK_CTRL1);
	wr(REF_CLK_CTRL1, v | REF_240M_CLK_EN);
	v = rd(REF_CLK_CTRL2);
	wr(REF_CLK_CTRL2, v | REF_160M_CLK_EN | REF_20M_CLK_EN);

	v = rd(PERI_CLK_CTRL02);
	v &= ~(3u << 30);
	wr(PERI_CLK_CTRL02, v);

	v = rd(PERI_CLK_CTRL03);
	v &= ~((3u << DPICLK_SRC_S) | (0xFFu << DPICLK_DIV_S) |
	       (7u << PLL_REFCLK_SRC_S) | (0xFFu << PLL_REFCLK_DIV_S));
	v |= (DPI_SRC_SEL << DPICLK_SRC_S) | ((DPI_DIV - 1u) << DPICLK_DIV_S) |
	     DPICLK_EN | DPHY_CFG_CLK_EN | DPHY_PLL_REFCLK_EN |
	     (0u << PLL_REFCLK_SRC_S) | (0u << PLL_REFCLK_DIV_S);
	wr(PERI_CLK_CTRL03, v);

	v = rd(HP_RST_EN0);
	wr(HP_RST_EN0, v | RST_EN_DSI_BRG);
	wr(HP_RST_EN0, v & ~RST_EN_DSI_BRG);
}

static void phy_tst_write(uint8_t addr, uint8_t val)
{
	host_wr(H_PHY_TST0, 0u);
	host_wr(H_PHY_TST1, (1u << 16) | (uint32_t)addr);
	host_wr(H_PHY_TST0, 2u);
	host_wr(H_PHY_TST0, 0u);
	host_wr(H_PHY_TST1, (uint32_t)val);
	host_wr(H_PHY_TST0, 2u);
	host_wr(H_PHY_TST0, 0u);
}

static void phy_pll_1000m(void)
{
	/*
	 * f_vco = M/N * f_ref.  peri_clk_ctrl02[31:30]=0 selects PLL_F20M,
	 * so f_ref is 20 MHz — XTAL is only selectable on P4 rev >= 3.0.
	 * Assuming 40 MHz here halves the link to 500 Mbps, which still
	 * locks but overruns the host DPI FIFO (int_st1.dpi_pld_wr_err)
	 * and leaves the data lanes parked in LP-11.
	 */
	phy_tst_write(0x44u, (uint8_t)(PLL_HS_FREQ_SEL << 1));
	phy_tst_write(0x19u, 0x30u);
	phy_tst_write(0x17u, (uint8_t)(PLL_N - 1u));
	phy_tst_write(0x18u, (uint8_t)((PLL_M - 1u) & 0x1Fu));
	phy_tst_write(0x18u,
		      (uint8_t)(0x80u | (((PLL_M - 1u) >> 5) & 0x0Fu)));
}

static int wait_phy_ready(void)
{
	uint32_t guard;
	uint32_t st;
	uint32_t mask;

	guard = 0u;
	while (!(host_rd(H_PHY_STATUS) & 1u) && guard < 200u) {
		(void)ulmk_sleep_ms(1u);
		guard++;
	}
	if (!(host_rd(H_PHY_STATUS) & 1u))
		return -1;

	mask = (1u << 2) | (1u << 4) | (1u << 7);
	guard = 0u;
	for (;;) {
		st = host_rd(H_PHY_STATUS);
		if ((st & mask) == mask)
			return 0;
		if (guard++ >= 200u)
			return -1;
		(void)ulmk_sleep_ms(1u);
	}
}

/*
 * Host lane-byte timing uses *expected* DPI (52), matching
 * mipi_dsi_hal_host_dpi_set_horizontal_timing().
 */
static uint32_t px_to_lane(uint32_t px)
{
	return (px * LANE_MBPS + (DPI_EXPECT_MHZ * 4u)) /
	       (DPI_EXPECT_MHZ * 8u);
}

static void wait_cmd_fifo(void)
{
	uint32_t guard = 0u;

	while ((host_rd(H_CMD_PKT_ST) & CMD_FIFO_FULL) && guard < 100000u)
		guard++;
}

static void wait_cmd_empty(void)
{
	uint32_t guard = 0u;

	while (!(host_rd(H_CMD_PKT_ST) & CMD_FIFO_EMPTY) && guard < 100000u)
		guard++;
}

static void dcs_short(uint8_t cmd, const uint8_t *param, uint8_t nparam)
{
	uint8_t dt;
	uint8_t lsb;
	uint8_t msb;

	if (nparam == 0u) {
		dt = DT_DCS_SHORT_0;
		lsb = cmd;
		msb = 0u;
	} else {
		dt = DT_DCS_SHORT_1;
		lsb = cmd;
		msb = param[0];
	}
	wait_cmd_fifo();
	/* VC0 in [7:6] — same as mipi_dsi_host_ll_gen_set_packet_header */
	host_wr(H_GEN_HDR, ((uint32_t)msb << 16) | ((uint32_t)lsb << 8) |
			   ((0u << 6) | dt));
}

static void ek79007_init_cmds(void)
{
	/* esp_lcd_ek79007 vendor_specific_init_default — no COLMOD/DISPON */
	static const uint8_t vendor[][2] = {
		{ 0x80u, 0x8Bu },
		{ 0x81u, 0x78u },
		{ 0x82u, 0x84u },
		{ 0x83u, 0x88u },
		{ 0x84u, 0xA8u },
		{ 0x85u, 0xE3u },
		{ 0x86u, 0x88u },
	};
	uint32_t i;
	uint8_t lane = 0x10u;

	dcs_short(0xB2u, &lane, 1u);
	for (i = 0u; i < sizeof(vendor) / sizeof(vendor[0]); i++)
		dcs_short(vendor[i][0], &vendor[i][1], 1u);
	dcs_short(0x11u, NULL, 0u);
	(void)ulmk_sleep_ms(120u);
	wait_cmd_empty();
}

static int32_t div_round(int32_t num, int32_t den)
{
	if (num >= 0)
		return (num + den / 2) / den;
	return (num - den / 2) / den;
}

static void bridge_video_timing(void)
{
	uint32_t htot = HSW + HBP + PANEL_H + HFP;
	uint32_t vtot = VSW + VBP + PANEL_V + VFP;
	int32_t comp;
	uint32_t hfp;

	/*
	 * IDF: compensate HFP so refresh matches expect when real DPI
	 * differs: round(real/expect * htot) - htot.
	 */
	comp = div_round((int32_t)(DPI_REAL_MHZ * htot),
			 (int32_t)DPI_EXPECT_MHZ) -
	       (int32_t)htot;
	hfp = (uint32_t)((int32_t)HFP + comp);
	htot = HSW + HBP + PANEL_H + hfp;

	brg_wr(BRG_EN, (1u << 1));
	brg_wr(BRG_EN, 0u);
	brg_wr(BRG_EN, 1u);
	brg_wr(BRG_HOST_CTRL, 1u);

	brg_wr(BRG_DPI_H_CFG0, (PANEL_H << 16) | htot);
	brg_wr(BRG_DPI_H_CFG1, (HSW << 16) | HBP);
	brg_wr(BRG_DPI_V_CFG0, (PANEL_V << 16) | vtot);
	brg_wr(BRG_DPI_V_CFG1, (VSW << 16) | VBP);
	/* dpi_en=0 until dsi_fb_start fills FIFO then enables DPI */
	brg_wr(BRG_DPI_MISC, (PANEL_H << 4));
	brg_wr(BRG_DPI_UPD, 1u);
}

static void host_video_timing(void)
{
	uint32_t hsa;
	uint32_t hbp;
	uint32_t act;
	uint32_t hfp;
	uint32_t htot;
	uint32_t sum;
	int32_t comp;

	hsa = px_to_lane(HSW);
	hbp = px_to_lane(HBP);
	act = px_to_lane(PANEL_H);
	hfp = px_to_lane(HFP);
	htot = px_to_lane(HSW + HBP + PANEL_H + HFP);
	sum = hsa + hbp + act + hfp;
	comp = (int32_t)htot - (int32_t)sum;
	act = (uint32_t)((int32_t)act + comp);

	host_wr(H_VID_HSA, hsa);
	host_wr(H_VID_HBP, hbp);
	host_wr(H_VID_HLINE, hsa + hbp + act + hfp);
	host_wr(H_VID_VSA, VSW);
	host_wr(H_VID_VBP, VBP);
	host_wr(H_VID_VFP, VFP);
	host_wr(H_VID_VACT, PANEL_V);
	host_wr(H_VID_PKT, PANEL_H);
	host_wr(H_VID_CHUNKS, 0u);
	host_wr(H_VID_NULL, 0u);
	/* VID_MODE LP bits set in dsi_init after DCS (IDF order). */
}

int dsi_init(void)
{
	uint32_t esc_div;
	uint32_t to_div;

	if (g_dsi_ok)
		return 0;

	ldo_vo3_mipi_2v5();
	dsi_clocks_enable();

	board_lcd_reset();

	host_wr(H_PHY_IF_CFG, ((LANE_NUM - 1u) & 3u) | (0x3Fu << 8));
	host_wr(H_PWR_UP, 1u);
	host_wr(H_PHY_RSTZ, PHY_SHUTDOWNZ);
	host_wr(H_PHY_RSTZ, PHY_SHUTDOWNZ | PHY_RSTZ);
	host_wr(H_PHY_RSTZ,
		PHY_SHUTDOWNZ | PHY_RSTZ | PHY_ENABLECLK | PHY_FORCEPLL);

	bridge_video_timing();

	phy_pll_1000m();
	if (wait_phy_ready() != 0) {
		board_console_printf("ulmk: dsi phy lock fail\n");
		return -1;
	}

	host_wr(H_MODE_CFG, 1u);
	host_wr(H_LPCLK_CTRL, 0u); /* clock lane LP while sending DCS */

	host_wr(H_PHY_TMR, (50u << 16) | 104u);
	host_wr(H_PHY_TMR_LPCLK, (46u << 16) | 128u);
	host_wr(H_PCKHDL, (1u << 0) | (1u << 3) | (1u << 4));

	esc_div = (LANE_MBPS + 8u * 18u / 2u) / (8u * 18u);
	if (esc_div < 1u)
		esc_div = 1u;
	to_div = (LANE_MBPS + 8u * 10u / 2u) / (8u * 10u);
	if (to_div < 1u)
		to_div = 1u;
	host_wr(H_CLKMGR, (to_div << 8) | esc_div);

	host_wr(H_CMD_MODE,
		(1u << 8) | (1u << 9) | (1u << 10) | (1u << 14) |
		(1u << 16) | (1u << 17) | (1u << 19));

	host_wr(H_DPI_VCID, 0u);
	host_wr(H_DPI_COLOR, 0u); /* RGB565 config1 — BSP default */

	ek79007_init_cmds();
	host_video_timing();

	/*
	 * Program VID_MODE now but stay in command mode.  Entering video
	 * with dpi_en=0 (no DMA) left this panel permanently black; IDF
	 * dpi_panel_init enables video only after DW_GDMA is running.
	 */
	host_wr(H_VID_MODE,
		2u |
		(1u << 8) | (1u << 9) | (1u << 10) | (1u << 11) |
		(1u << 12) | (1u << 13) | (1u << 14) | (1u << 15));
	host_wr(H_MODE_CFG, 1u); /* command until dsi_video_enable() */
	host_wr(H_LPCLK_CTRL, 0u); /* STATE_LP while idle/DCS */

	g_dsi_ok = 1;
	board_console_printf("ulmk: dsi ek79007 cmd ok (video deferred)\n");
	return 0;
}

void dsi_video_enable(void)
{
	if (!g_dsi_ok)
		return;

	host_wr(H_MODE_CFG, 0u); /* video */
	/*
	 * IDF MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO:
	 *   auto_clklane_ctrl=1, phy_txrequestclkhs=1 → 0x3
	 * Writing 0 is STATE_LP (no HS clock) → permanent black panel
	 * with underrun=0 (DPI never consumes).
	 */
	host_wr(H_LPCLK_CTRL, (1u << 0) | (1u << 1));
	board_console_printf("ulmk: dsi video on lpclk=0x3\n");
}

void dsi_host_dump(void)
{
	board_console_printf("ulmk: dsi host mode=0x%x vid=0x%08x act=0x%08x "
		   "lpclk=0x%x pwrup=0x%x\n",
		   (unsigned)host_rd(H_MODE_CFG),
		   (unsigned)host_rd(H_VID_MODE),
		   (unsigned)host_rd(H_VID_MODE_ACT),
		   (unsigned)host_rd(H_LPCLK_CTRL),
		   (unsigned)host_rd(H_PWR_UP));
	board_console_printf("ulmk: dsi host color=0x%x/act=0x%x pkt=%u hline=%u "
		   "vact=%u vsa=%u vbp=%u vfp=%u hsa=%u hbp=%u\n",
		   (unsigned)host_rd(H_DPI_COLOR),
		   (unsigned)host_rd(H_DPI_COLOR_ACT),
		   (unsigned)host_rd(H_VID_PKT),
		   (unsigned)host_rd(H_VID_HLINE),
		   (unsigned)host_rd(H_VID_VACT),
		   (unsigned)host_rd(H_VID_VSA),
		   (unsigned)host_rd(H_VID_VBP),
		   (unsigned)host_rd(H_VID_VFP),
		   (unsigned)host_rd(H_VID_HSA),
		   (unsigned)host_rd(H_VID_HBP));
	/*
	 * int_st1.dpi_pld_wr_err (bit 7) means the bridge outruns the link:
	 * the usual cause is a PHY PLL programmed for the wrong reference.
	 */
	board_console_printf("ulmk: dsi host phy=0x%08x err0=0x%08x err1=0x%08x\n",
		   (unsigned)host_rd(H_PHY_STATUS),
		   (unsigned)host_rd(H_INT_ST0),
		   (unsigned)host_rd(H_INT_ST1));
}

int dsi_ready(void)
{
	return g_dsi_ok;
}
