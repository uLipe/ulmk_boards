/* SPDX-License-Identifier: MIT */
/*
 * PSRAM (hex AP) — MPLL + ROM MSPI user cmds + AXI window map.
 * Call only after .bss is cleared (e.g. board_services_init).
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "board_config.h"
#include "board_psram.h"
#include "board_mpll.h"
#include "board_cache.h"

#include "board_console.h"
#include "board_psram_tune.h"

typedef enum {
	ESP_ROM_SPIFLASH_QIO_MODE = 0,
	ESP_ROM_SPIFLASH_QOUT_MODE,
	ESP_ROM_SPIFLASH_DIO_MODE,
	ESP_ROM_SPIFLASH_DOUT_MODE,
	ESP_ROM_SPIFLASH_FASTRD_MODE,
	ESP_ROM_SPIFLASH_SLOWRD_MODE,
	ESP_ROM_SPIFLASH_OPI_STR_MODE,
	ESP_ROM_SPIFLASH_OPI_DTR_MODE,
} esp_rom_spiflash_read_mode_t;

typedef struct {
	uint16_t cmd;
	uint16_t cmdBitLen;
	uint32_t *addr;
	uint32_t addrBitLen;
	uint32_t *txData;
	uint32_t txDataBitLen;
	uint32_t *rxData;
	uint32_t rxDataBitLen;
	uint32_t dummyBitLen;
} esp_rom_spi_cmd_t;

void esp_rom_spi_set_op_mode(int spi_num, esp_rom_spiflash_read_mode_t mode);
void esp_rom_spi_cmd_config(int spi_num, esp_rom_spi_cmd_t *pcmd);
void esp_rom_spi_cmd_start(int spi_num, uint8_t *rx_buf, uint16_t rx_len,
			   uint8_t cs_en_mask, bool is_write_erase);
void Cache_PSRAM_MMU_Init(void);
int Cache_PSRAM_MMU_Set(uint32_t sensitive, uint32_t vaddr, uint32_t paddr,
			uint32_t psize, uint32_t num, uint32_t fixed);

#define HP_CLKRST		0x500E6000u
#define HP_SOC_CLK_CTRL0	(HP_CLKRST + 0x14u)
#define HP_PERI_CLK_CTRL00	(HP_CLKRST + 0x30u)
#define HP_RST_EN0		(HP_CLKRST + 0xc0u)

#define PSRAM_SYS_CLK_EN	(1u << 31)
#define PSRAM_CLK_SRC_MASK	(3u << 12)
#define PSRAM_CLK_SRC_MPLL	(1u << 12)
#define PSRAM_PLL_CLK_EN	(1u << 14)
#define PSRAM_CORE_CLK_EN	(1u << 15)
#define RST_DUAL_MSPI_AXI	(1u << 23)
#define RST_DUAL_MSPI_APB	(1u << 25)

#define SPIMEM2			0x5008E000u
#define SPIMEM3			0x5008F000u
#define MSPI_ID_3		3

#define CACHE_FCTRL		(SPIMEM2 + 0x3cu)
#define CACHE_SCTRL		(SPIMEM2 + 0x40u)
#define SRAM_CMD		(SPIMEM2 + 0x44u)
#define SRAM_DRD_CMD		(SPIMEM2 + 0x48u)
#define SRAM_DWR_CMD		(SPIMEM2 + 0x4cu)
#define SRAM_CLK		(SPIMEM2 + 0x50u)
#define CTRL1			(SPIMEM2 + 0x0cu)
#define SMEM_DDR		(SPIMEM2 + 0xd8u)
#define TIMING_CALI		(SPIMEM2 + 0x180u)
#define SMEM_TIMING_CALI	(SPIMEM2 + 0x190u)
#define SMEM_AC			(SPIMEM2 + 0x1a0u)
#define SPI1_CLOCK		(SPIMEM3 + 0x14u)

#define IOMUX_MSPI		0x500E1200u
#define PSRAM_DQS0_REG		(IOMUX_MSPI + 0x3cu)
#define PSRAM_DQS1_REG		(IOMUX_MSPI + 0x68u) /* IDF DQS_1_PIN0 */
#define DQS_XPD			(1u << 0)

#define HP_SYS_BASE		0x500E5000u
#define HP_CORE_ERR_RESP_DIS	(HP_SYS_BASE + 0x1a4u)

#define AXI_REQ_EN		(1u << 0)
#define CLOSE_AXI_INF_EN	(1u << 31)
#define USR_SADDR_4BYTE		(1u << 0)
#define USR_WR_SRAM_DUMMY	(1u << 3)
#define USR_RD_SRAM_DUMMY	(1u << 4)
#define CACHE_USR_RCMD		(1u << 5)
#define CACHE_USR_WCMD		(1u << 20)
#define SRAM_OCT		(1u << 21)
#define SDIN_OCT		(1u << 18)
#define SDOUT_OCT		(1u << 19)
#define SADDR_OCT		(1u << 20)
#define SCMD_OCT		(1u << 21)
#define SDIN_HEX		(1u << 26)
#define SDOUT_HEX		(1u << 27)
#define SMEM_DDR_EN		(1u << 0)
#define SMEM_VAR_DUMMY		(1u << 1)
#define AR_SPLICE_EN		(1u << 25)
#define AW_SPLICE_EN		(1u << 26)
#define DLL_TIMING_CALI		(1u << 5)

#define HEX_SYNC_READ		0x0000u
#define HEX_SYNC_WRITE		0x8080u
#define HEX_REG_WRITE		0xC0C0u
#define HEX_REF			0x5a6b7c8du
/*
 * Match esp_psram_impl_ap_hex.c CONFIG_SPIRAM_SPEED_200M — IDF LVGL demo
 * uses 200 MHz; 80 MHz underruns DPI (~70 MB/s) under GDMA load.
 */
#define PSRAM_BUS_MHZ		200u
#define PSRAM_FREQDIV		(400u / PSRAM_BUS_MHZ) /* MPLL 400 / 200 = 2 */
#define RD_DUMMY		(2u * (14u - 1u))
#define WR_DUMMY		(2u * (7u - 1u))
#define RD_LATENCY		4u
#define WR_LATENCY		1u
/* 8 MiB — dual 1024×600 RGB565 FBs + LVGL tlsf heap after them. */
#define MAP_PAGES		128u
#define PAGE_SHIFT		16u

static int g_psram_ok;
static int g_axi_ok;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static void settle_brief(void)
{
	volatile uint32_t i;

	for (i = 0u; i < 800u; i++)
		;
}

void board_psram_hex_xact(uint32_t cmd, uint32_t addr, uint32_t dummy,
			  uint32_t *tx, uint32_t tx_bits,
			  uint32_t *rx, uint32_t rx_bits)
{
	esp_rom_spi_cmd_t conf;
	uint32_t a = addr;

	esp_rom_spi_set_op_mode(MSPI_ID_3, ESP_ROM_SPIFLASH_OPI_DTR_MODE);
	conf.cmd = (uint16_t)cmd;
	conf.cmdBitLen = 16;
	conf.addr = &a;
	conf.addrBitLen = 32;
	conf.txData = tx;
	conf.txDataBitLen = tx_bits;
	conf.rxData = rx;
	conf.rxDataBitLen = rx_bits;
	conf.dummyBitLen = dummy;
	esp_rom_spi_cmd_config(MSPI_ID_3, &conf);
	esp_rom_spi_cmd_start(MSPI_ID_3, (uint8_t *)rx,
			      (uint16_t)(rx_bits / 8u), (1u << 1), false);
}

static void hex_xact(uint32_t cmd, uint32_t addr, uint32_t dummy,
		     uint32_t *tx, uint32_t tx_bits,
		     uint32_t *rx, uint32_t rx_bits)
{
	board_psram_hex_xact(cmd, addr, dummy, tx, tx_bits, rx, rx_bits);
}

static int probe_chip(void)
{
	uint32_t w = HEX_REF;
	uint32_t r = 0u;

	hex_xact(HEX_SYNC_WRITE, 0u, WR_DUMMY, &w, 32u, NULL, 0u);
	hex_xact(HEX_SYNC_READ, 0u, RD_DUMMY, NULL, 0u, &r, 32u);
	board_console_printf("ulmk: psram probe r=0x%08x\n", (unsigned)r);
	return (r == HEX_REF) ? 0 : -1;
}

static int map_window(void)
{
	int rc;

	Cache_PSRAM_MMU_Init();
	/* psize=64 (KiB pages); map MAP_PAGES @ 0x48000000 → paddr 0 */
	rc = Cache_PSRAM_MMU_Set(0u, ULMK_BOARD_PSRAM_BASE, 0u, 64u,
				 MAP_PAGES, 0u);
	board_console_printf("ulmk: psram mmu rc=%d pages=%u\n", rc,
		   (unsigned)MAP_PAGES);
	return (rc == 0) ? 0 : -1;
}

static void clocks_and_pins(void)
{
	uint32_t v;
	/*
	 * MPLL 400 / freqdiv = PSRAM bus.  IDF LVGL on this kit uses
	 * CONFIG_SPIRAM_SPEED_200M — required headroom for DW_GDMA→DPI.
	 */
	uint32_t freqdiv = PSRAM_FREQDIV;
	uint32_t freqbits;
	uint32_t ac;

	v = rd(HP_SOC_CLK_CTRL0);
	wr(HP_SOC_CLK_CTRL0, v | PSRAM_SYS_CLK_EN);

	v = rd(HP_PERI_CLK_CTRL00);
	v &= ~PSRAM_CLK_SRC_MASK;
	v |= PSRAM_CLK_SRC_MPLL | PSRAM_PLL_CLK_EN | PSRAM_CORE_CLK_EN;
	/* core_clk_div_num = 0 → divide-by-1 (IDF MSPI_TIMING_CORE_CLOCK_DIV) */
	v &= ~(0xFFu << 16);
	wr(HP_PERI_CLK_CTRL00, v);

	v = rd(HP_RST_EN0);
	wr(HP_RST_EN0, v | RST_DUAL_MSPI_AXI | RST_DUAL_MSPI_APB);
	wr(HP_RST_EN0, v & ~(RST_DUAL_MSPI_AXI | RST_DUAL_MSPI_APB));
	settle_brief();

	/*
	 * IDF mspi_timing_ll_pin_drv_set(2) + enable_dqs: every PSRAM pad at
	 * drive strength 2.  Leaving data/CK/CS at reset drive is enough for
	 * the idle probe, but bit-errors show up under GDMA+writeback @200M.
	 */
	{
		uint32_t i;
		uint32_t v;

		for (i = 0u; i < 8u; i++) {
			v = rd(IOMUX_MSPI + 0x1cu + i * 4u);
			v = (v & ~(3u << 12)) | (2u << 12);
			wr(IOMUX_MSPI + 0x1cu + i * 4u, v);
		}
		for (i = 0u; i < 10u; i++) {
			v = rd(IOMUX_MSPI + 0x40u + i * 4u);
			v = (v & ~(3u << 12)) | (2u << 12);
			wr(IOMUX_MSPI + 0x40u + i * 4u, v);
		}
		wr(PSRAM_DQS0_REG, (rd(PSRAM_DQS0_REG) & ~(3u << 15)) | DQS_XPD |
		   (2u << 15));
		wr(PSRAM_DQS1_REG, (rd(PSRAM_DQS1_REG) & ~(3u << 15)) | DQS_XPD |
		   (2u << 15));
	}

	freqbits = ((freqdiv - 1u) << 0) | ((freqdiv / 2u - 1u) << 8) |
		   ((freqdiv - 1u) << 16);
	wr(SRAM_CLK, freqbits);
	wr(SPI1_CLOCK, freqbits);
	board_console_printf("ulmk: psram bus %u MHz (div=%u)\n",
		   (unsigned)(400u / freqdiv), (unsigned)freqdiv);

	wr(TIMING_CALI, rd(TIMING_CALI) | DLL_TIMING_CALI);
	wr(SMEM_TIMING_CALI, rd(SMEM_TIMING_CALI) | DLL_TIMING_CALI);

	/* CS setup/hold/hold_delay — match IDF AP_HEX_PSRAM_CS_* */
	ac = rd(SMEM_AC);
	ac |= (1u << 0) | (1u << 1) | (1u << 31); /* setup, hold, split */
	ac &= ~((0x1Fu << 2) | (0x1Fu << 7) | (0x3Fu << 25));
	ac |= ((4u - 1u) << 2) | ((4u - 1u) << 7) | ((3u - 1u) << 25);
	wr(SMEM_AC, ac);

	/* page_size=2048 → SMEM_PAGE_SIZE=3 @ bits[19:18] */
	wr(SPIMEM2 + 0x174u,
	   (rd(SPIMEM2 + 0x174u) & ~(3u << 18)) | (3u << 18));
}

static void init_mode_regs(void)
{
	/* IDF hex_psram_mode_reg @ 200 MHz */
	uint32_t mr0 = (1u << 5) | ((RD_LATENCY & 7u) << 2); /* lt=1, rl */
	uint32_t mr4 = ((WR_LATENCY & 7u) << 5);
	uint32_t mr8 = 3u | (1u << 3) | (1u << 6); /* bl=3, rbx, x16 */

	hex_xact(HEX_REG_WRITE, 0x0u, 0u, &mr0, 16u, NULL, 0u);
	hex_xact(HEX_REG_WRITE, 0x4u, 0u, &mr4, 16u, NULL, 0u);
	hex_xact(HEX_REG_WRITE, 0x8u, 0u, &mr8, 16u, NULL, 0u);
}

static void config_axi(void)
{
	uint32_t sctrl;
	uint32_t scmd;
	uint32_t fctrl;

	/* cmd_bitlen is N-1 in the high nibble (IDF / esp-hal). */
	wr(SRAM_DRD_CMD, ((16u - 1u) << 28) | HEX_SYNC_READ);
	wr(SRAM_DWR_CMD, ((16u - 1u) << 28) | HEX_SYNC_WRITE);

	sctrl = rd(CACHE_SCTRL);
	sctrl |= USR_SADDR_4BYTE | USR_WR_SRAM_DUMMY | USR_RD_SRAM_DUMMY |
		 CACHE_USR_RCMD | CACHE_USR_WCMD | SRAM_OCT;
	sctrl &= ~((0x3Fu << 6) | (0x3Fu << 14) | (0x3Fu << 22));
	sctrl |= ((RD_DUMMY - 1u) << 6);
	sctrl |= ((32u - 1u) << 14);
	sctrl |= ((WR_DUMMY - 1u) << 22);
	wr(CACHE_SCTRL, sctrl);

	scmd = rd(SRAM_CMD);
	scmd |= SDIN_OCT | SDOUT_OCT | SADDR_OCT | SCMD_OCT |
		SDIN_HEX | SDOUT_HEX | (1u << 23); /* sdummy_wout */
	wr(SRAM_CMD, scmd);

	wr(SMEM_DDR, rd(SMEM_DDR) | SMEM_DDR_EN | SMEM_VAR_DUMMY);
	wr(CTRL1, rd(CTRL1) | AR_SPLICE_EN | AW_SPLICE_EN);

	fctrl = rd(CACHE_FCTRL);
	fctrl |= AXI_REQ_EN;
	fctrl &= ~CLOSE_AXI_INF_EN;
	wr(CACHE_FCTRL, fctrl);
}

int board_psram_enable_axi(void)
{
	volatile uint32_t *p;
	uint32_t i;
	uint32_t expect;
	uint32_t got;

	if (!g_psram_ok)
		return -1;
	if (g_axi_ok)
		return 0;

	/*
	 * IDF: disable core DECERR response before any AXI/MMU touch so a
	 * bad first access becomes bus error data, not a hart hang.
	 */
	wr(HP_CORE_ERR_RESP_DIS, 0x7u);
	board_console_printf("ulmk: psram axi err_resp off\n");

	/*
	 * IDF order: configure MSPI2 for PSRAM, timing-tune, then MMU map.
	 * Tuning itself talks MSPI3 user cmds + IOMUX delaylines; the MSPI2
	 * side must already be in the same line/DDR mode the AXI path uses.
	 */
	config_axi();
	board_console_printf("ulmk: psram axi cfg\n");
	/* IDF clears variable-dummy for the MSPI3 sweep; mirror on MSPI2. */
	wr(SMEM_DDR, rd(SMEM_DDR) & ~SMEM_VAR_DUMMY);
	if (board_psram_timing_tune() != 0)
		return -1;
	wr(SMEM_DDR, rd(SMEM_DDR) | SMEM_VAR_DUMMY);
	board_psram_timing_apply();
	if (map_window() != 0)
		return -1;
	/*
	 * SPI-user-cmd tuning can land on a point that is still marginal on
	 * the AXI+cache path the LVGL flush uses.  Refine against that path
	 * before handing the window to userspace.
	 */
	if (board_psram_timing_refine_axi(
		    (void *)(uintptr_t)(ULMK_BOARD_PSRAM_BASE + 0x1000u),
		    4096u) != 0)
		return -1;

	/*
	 * Do not Cache_Invalidate_Addr before the first store — on a cold
	 * window that ROM path can wedge waiting on AXI.  Fence only.
	 */
	board_console_printf("ulmk: psram axi r/w...\n");
	p = (volatile uint32_t *)(uintptr_t)ULMK_BOARD_PSRAM_BASE;
	p[0] = 0xA5A55A5Au;
	__asm__ volatile("fence rw, rw" ::: "memory");
	settle_brief();
	got = p[0];
	if (got != 0xA5A55A5Au) {
		board_console_printf("ulmk: psram axi fail got=0x%08x\n",
			   (unsigned)got);
		return -1;
	}
	for (i = 1u; i < 8u; i++) {
		expect = 0xA5000000u ^ (i * 0x11111111u);
		p[i] = expect;
	}
	__asm__ volatile("fence rw, rw" ::: "memory");
	for (i = 1u; i < 8u; i++) {
		expect = 0xA5000000u ^ (i * 0x11111111u);
		got = p[i];
		if (got != expect) {
			board_console_printf("ulmk: psram axi fail @%u got=0x%08x\n",
				   (unsigned)i, (unsigned)got);
			return -1;
		}
	}

	board_dcache_invalidate((const void *)(uintptr_t)ULMK_BOARD_PSRAM_BASE,
				4096u);

	g_axi_ok = 1;
	board_console_printf("ulmk: psram axi ok base=0x%08x\n",
		   (unsigned)ULMK_BOARD_PSRAM_BASE);
	return 0;
}

int board_psram_init(void)
{
	if (g_psram_ok)
		return 0;

	board_console_printf("ulmk: psram init\n");

	if (board_mpll_enable_400m() != 0)
		return -1;

	clocks_and_pins();
	init_mode_regs();

	if (probe_chip() != 0) {
		board_console_printf("ulmk: psram probe fail\n");
		return -1;
	}

	g_psram_ok = 1;
	if (board_psram_enable_axi() != 0) {
		board_console_printf("ulmk: psram probe ok (axi deferred)\n");
		return 0;
	}
	return 0;
}

void *board_psram_base(void)
{
	return g_axi_ok ? (void *)(uintptr_t)ULMK_BOARD_PSRAM_BASE : NULL;
}

size_t board_psram_size(void)
{
	return g_axi_ok ? (size_t)(MAP_PAGES << PAGE_SHIFT) : 0u;
}

int board_psram_ready(void)
{
	return g_axi_ok;
}

void *board_psram_nc(void *cached)
{
	uintptr_t a = (uintptr_t)cached;

	if (!g_axi_ok || !cached)
		return NULL;
	if (a < ULMK_BOARD_PSRAM_BASE ||
	    a >= ULMK_BOARD_PSRAM_BASE + (MAP_PAGES << PAGE_SHIFT))
		return NULL;
	return (void *)(uintptr_t)(a + 0x40000000u);
}
