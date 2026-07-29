/* SPDX-License-Identifier: MIT */
/*
 * dsi_fb.c — RGB565 PSRAM FB → DW_GDMA → MIPI-DSI DPI.
 *
 * Match esp_lcd dpi_panel_init + mipi_dsi_dma_trans_done_cb:
 *   - singly LLI with LLI_LAST; ISR re-sets VALID and re-enables channel
 *   - (circular without refreshing VALID stalls after 1 frame → underrun)
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_cache.h"
#include "dsi.h"

#include "board_console.h"

#define DW_GDMA_BASE		0x50081000u
#define DSI_BRG_MEM		0x50105000u
#define BRG_BASE		ULMK_BOARD_DSI_BRG_BASE
#define HOST_BASE		ULMK_BOARD_DSI_HOST_BASE

#define HP_CLKRST		0x500E6000u
#define SOC_CLK_CTRL0		(HP_CLKRST + 0x14u)
#define SOC_CLK_CTRL1		(HP_CLKRST + 0x18u)
#define HP_RST_EN0		(HP_CLKRST + 0xc0u)
#define GDMA_CPU_CLK_EN		(1u << 13)
#define GDMA_SYS_CLK_EN		(1u << 5)
#define RST_EN_GDMA		(1u << 21)

#define PANEL_H			1024u
#define PANEL_V			600u
#define FB_BYTES		(PANEL_H * PANEL_V * 2u)
#define BLOCK_ITEMS		(FB_BYTES / 8u)

#define BRG_DMA_REQ_CFG		0x08u
#define BRG_RAW_NUM		0x0cu
#define BRG_PIXEL_TYPE		0x18u
#define BRG_DPI_MISC		0x40u
#define BRG_DPI_UPD		0x44u
#define BRG_DMA_FLOW		0x88u
#define BRG_EMPTY_THRD		0x8cu
#define BRG_EN			0x04u
#define BRG_INT_ENA		0x50u
#define BRG_INT_CLR		0x54u
#define BRG_INT_ST		0x5cu
#define BRG_UNDERRUN		(1u << 0)

#define CH_BASE			(DW_GDMA_BASE + 0x100u)
#define CH_SAR0			(CH_BASE + 0x00u)
#define CH_CFG0			(CH_BASE + 0x20u)
#define CH_CFG1			(CH_BASE + 0x24u)
#define CH_LLP0			(CH_BASE + 0x28u)
#define CH_INT_ST_ENA0		(CH_BASE + 0x80u)
#define CH_INT_ST0		(CH_BASE + 0x88u)
#define CH_INT_SIG_ENA0		(CH_BASE + 0x90u)
#define CH_INT_CLR0		(CH_BASE + 0x98u)

#define DMAC_CFG0		(DW_GDMA_BASE + 0x10u)
#define DMAC_CHEN0		(DW_GDMA_BASE + 0x18u)
#define DMAC_CHEN1		(DW_GDMA_BASE + 0x1cu)
#define DMAC_RESET0		(DW_GDMA_BASE + 0x58u)

#define DMA_TFR_DONE		(1u << 1)

#define CTL0_SMS_MEM		(1u << 0)
#define CTL0_DINC_FIXED		(1u << 6)
#define CTL0_WIDTH64		((3u << 8) | (3u << 11))
#define CTL0_SRC_MSIZE_512	(7u << 14)
#define CTL0_DST_MSIZE_256	(6u << 18)

#define CTL1_ARLEN		((1u << 6) | (16u << 7))
#define CTL1_AWLEN		((1u << 15) | (16u << 16))
#define CTL1_IOC_BLK		(1u << 26)
#define CTL1_LLI_LAST		(1u << 30)
#define CTL1_LLI_VALID		(1u << 31)

struct dsi_lli {
	uint32_t sar_lo;
	uint32_t sar_hi;
	uint32_t dar_lo;
	uint32_t dar_hi;
	uint32_t block_ts;
	uint32_t reserved_14;
	uint32_t llp_lo;
	uint32_t llp_hi;
	uint32_t ctl0;
	uint32_t ctl1;
	uint32_t sstat;
	uint32_t dstat;
	uint32_t status_lo;
	uint32_t status_hi;
	uint32_t reserved_38;
	uint32_t reserved_3c;
} __attribute__((aligned(64)));

static struct dsi_lli g_lli;
static volatile void *g_fb;
static volatile int g_fb_on;
static volatile uint32_t g_rearms;
static int g_irq_bound;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void gdma_clocks(void)
{
	uint32_t v;

	v = rd(SOC_CLK_CTRL0);
	wr(SOC_CLK_CTRL0, v | GDMA_CPU_CLK_EN);
	v = rd(SOC_CLK_CTRL1);
	wr(SOC_CLK_CTRL1, v | GDMA_SYS_CLK_EN);

	v = rd(HP_RST_EN0);
	wr(HP_RST_EN0, v | RST_EN_GDMA);
	wr(HP_RST_EN0, v & ~RST_EN_GDMA);

	wr(DMAC_RESET0, 1u);
	while (rd(DMAC_RESET0) & 1u)
		;
	wr(DMAC_CFG0, 0x3u);
}

static void channel_abort(void)
{
	wr(DMAC_CHEN1, 0x101u);
	while (rd(DMAC_CHEN1) & 0x101u)
		;
	wr(CH_INT_CLR0, 0xFFFFFFFFu);
}

/*
 * The LLI is only ever written through the non-cacheable alias, so it never
 * holds dirty lines and needs no cache maintenance.  That matters because
 * the rearm runs in ISR context, where the ROM cache routines are not safe
 * to call (they are not reentrant against an interrupted thread).
 * The DMA is still pointed at the real address.
 */
#define LLI_NC_OFFSET		0x40000000u

static inline volatile struct dsi_lli *lli_nc(void)
{
	return (volatile struct dsi_lli *)((uintptr_t)&g_lli + LLI_NC_OFFSET);
}

static void lli_program(void *fb)
{
	volatile struct dsi_lli *l = lli_nc();

	memset((void *)l, 0, sizeof(*l));
	l->sar_lo = (uint32_t)(uintptr_t)fb;
	l->dar_lo = DSI_BRG_MEM;
	l->block_ts = BLOCK_ITEMS - 1u;
	l->llp_lo = 0u; /* singly — end of list */
	l->ctl0 = CTL0_SMS_MEM | CTL0_DINC_FIXED | CTL0_WIDTH64 |
		  CTL0_SRC_MSIZE_512 | CTL0_DST_MSIZE_256;
	l->ctl1 = CTL1_ARLEN | CTL1_AWLEN | CTL1_IOC_BLK |
		  CTL1_LLI_LAST | CTL1_LLI_VALID;
}

static void lli_revalidate(void)
{
	volatile struct dsi_lli *l = lli_nc();

	l->sar_lo = (uint32_t)(uintptr_t)g_fb;
	l->ctl1 = CTL1_ARLEN | CTL1_AWLEN | CTL1_IOC_BLK |
		  CTL1_LLI_LAST | CTL1_LLI_VALID;
}

static void channel_cfg(void)
{
	wr(CH_CFG0, (3u << 0) | (3u << 2));
	wr(CH_CFG1, (1u << 0) | (0u << 3) | (0u << 4) | (0u << 12) |
		    (5u << 23) | (2u << 27));
	wr(CH_INT_ST_ENA0, 0xFFFFFFFFu);
	wr(CH_INT_SIG_ENA0, DMA_TFR_DONE);
}

static void channel_start(void)
{
	uint32_t addr = (uint32_t)(uintptr_t)&g_lli;

	wr(CH_LLP0, ((addr >> 6) << 6) | 1u);
	wr(CH_INT_CLR0, 0xFFFFFFFFu);
	wr(DMAC_CHEN0, 0x101u);
}

static void bridge_dpi_off(void)
{
	wr(BRG_BASE + BRG_DPI_MISC, (PANEL_H << 4));
	wr(BRG_BASE + BRG_DPI_UPD, 1u);
}

static void bridge_dpi_on(void)
{
	wr(BRG_BASE + BRG_DPI_MISC, (PANEL_H << 4) | 1u);
	wr(BRG_BASE + BRG_DPI_UPD, 1u);
}

static void bridge_dma_cfg(void)
{
	uint32_t pix_bits = PANEL_H * PANEL_V * 16u;

	wr(BRG_BASE + BRG_EN, 1u);
	wr(BRG_BASE + BRG_PIXEL_TYPE, 2u);
	wr(BRG_BASE + BRG_RAW_NUM, (pix_bits / 64u) | (1u << 31));
	wr(BRG_BASE + BRG_DMA_FLOW, (1u << 4));
	wr(BRG_BASE + BRG_DMA_REQ_CFG, 256u);
	wr(BRG_BASE + BRG_EMPTY_THRD, 1024u - 256u);
	wr(BRG_BASE + BRG_INT_CLR, BRG_UNDERRUN);
	wr(BRG_BASE + BRG_INT_ENA, BRG_UNDERRUN);
	bridge_dpi_off();
}

/*
 * ISR fast path (ulmk_irq_attach_hw): the LLI must be revalidated and the
 * channel restarted before the DPI bridge drains, so this cannot wait for a
 * worker wakeup.  No syscalls here.  Returning false keeps ownership of the
 * peripheral ack/rearm and asks for no notification — nothing waits on one.
 */
static bool dw_gdma_rearm(void *data)
{
	uint32_t st;

	(void)data;
	if (!g_fb_on)
		return false;

	st = rd(CH_INT_ST0);
	if ((st & DMA_TFR_DONE) == 0u)
		return false;

	wr(CH_INT_CLR0, st);
	lli_revalidate();
	channel_start();
	g_rearms++;
	return false;
}

int dsi_fb_start(void *fb, uint32_t nbytes)
{
	uint32_t und;

	if (!fb || nbytes < FB_BYTES)
		return -1;
	if (g_fb_on)
		return 0;
	if (!dsi_ready())
		return -1;

	g_fb = fb;

	if (!g_irq_bound) {
		ulmk_notif_t n;

		n = ulmk_irq_attach_hw(ULMK_BOARD_IRQ_DW_GDMA, dw_gdma_rearm,
				       NULL,
				       ULMK_BOARD_CLIC_BASE + 0x1000u +
				       ULMK_BOARD_CLIC_IRQ_DW_GDMA * 4u);
		if ((int32_t)n < 0 && (int32_t)n >= -16)
			return -1;
		if (ulmk_irq_enable(ULMK_BOARD_IRQ_DW_GDMA) != ULMK_OK)
			return -1;
		g_irq_bound = 1;
	}

	gdma_clocks();
	channel_abort();
	bridge_dma_cfg();
	channel_cfg();
	/*
	 * No ROM cache maintenance here: the FB is only ever written
	 * through the non-cacheable alias, so no dirty lines exist and
	 * Cache_WriteBack_Addr from user mode traps on this window.
	 */
	__asm__ volatile("fence rw, rw" ::: "memory");
	lli_program(fb);

	/* IDF dpi_panel_init: DMA → video mode → dpi_en */
	g_fb_on = 1;
	channel_start();
	(void)ulmk_sleep_ms(10u);
	dsi_video_enable();
	bridge_dpi_on();

	(void)ulmk_sleep_ms(200u);
	und = rd(BRG_BASE + BRG_INT_ST) & BRG_UNDERRUN;
	if (und)
		wr(BRG_BASE + BRG_INT_CLR, BRG_UNDERRUN);

	board_console_printf("ulmk: dsi dma rearm=%u chen=0x%x underrun=%u\n",
		   (unsigned)g_rearms,
		   (unsigned)(rd(DMAC_CHEN0) & 0xFu),
		   (unsigned)und);
	board_console_printf("ulmk: dsi fb stream @%p\n", fb);
	return 0;
}

uint32_t dsi_fb_frames(void)
{
	return g_rearms;
}

void dsi_fb_diag(uint32_t window_ms)
{
	uint32_t before;
	uint32_t after;
	uint32_t sar_a;
	uint32_t sar_b;

	before = g_rearms;
	sar_a = rd(CH_SAR0);
	(void)ulmk_sleep_ms(window_ms);
	after = g_rearms;
	sar_b = rd(CH_SAR0);

	/*
	 * frames/s tells whether the DPI is pacing the DMA: a 60 Hz panel
	 * must show ~60.  Much higher = handshake ignored (FIFO overrun),
	 * 0 = channel stalled.
	 */
	board_console_printf("ulmk: dsi diag frames=%u/%ums chen=0x%x llp=0x%08x\n",
		   (unsigned)(after - before), (unsigned)window_ms,
		   (unsigned)(rd(DMAC_CHEN0) & 0xFu),
		   (unsigned)rd(CH_LLP0));
	board_console_printf("ulmk: dsi diag sar=0x%08x->0x%08x intst=0x%08x "
		   "ctl1=0x%08x\n",
		   (unsigned)sar_a, (unsigned)sar_b,
		   (unsigned)rd(CH_INT_ST0), (unsigned)g_lli.ctl1);
	board_console_printf("ulmk: dsi diag brg en=0x%x misc=0x%08x int=0x%08x "
		   "pixel=0x%08x raw=0x%08x\n",
		   (unsigned)rd(BRG_BASE + BRG_EN),
		   (unsigned)rd(BRG_BASE + BRG_DPI_MISC),
		   (unsigned)rd(BRG_BASE + BRG_INT_ST),
		   (unsigned)rd(BRG_BASE + BRG_PIXEL_TYPE),
		   (unsigned)rd(BRG_BASE + BRG_RAW_NUM));
	dsi_host_dump();
}

int dsi_fb_set(void *fb)
{
	if (!g_fb_on || !fb)
		return -1;
	g_fb = fb;
	return 0;
}

int dsi_fb_ready(void)
{
	return g_fb_on;
}
