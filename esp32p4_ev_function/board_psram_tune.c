/* SPDX-License-Identifier: MIT */
/*
 * PSRAM MSPI timing tuning — ESP32-P4 hex AP @ 200 MHz DTR.
 *
 * Port of IDF mspi_timing_by_dqs.c / mspi_timing_psram_tuning():
 *   1. write a known pattern at 20 MHz (safe)
 *   2. sweep DQS phase at 200 MHz, keep the start of the longest good window
 *   3. sweep data/DQS delaylines (31 points x 100 reads), keep the middle
 *   4. program the winners into the IOMUX MSPI pin group
 *
 * Without this, AXI traffic under DW_GDMA writeback flips bits in PSRAM
 * (see psram_stress: expect^got == 0x00040000).
 */
#include <stdint.h>
#include <stddef.h>
#include "board_console.h"
#include "board_cache.h"
#include "board_psram_tune.h"

/* Shared with board_psram.c — same MSPI / IOMUX bases. */
#define SPIMEM2			0x5008E000u
#define SPIMEM3			0x5008F000u
#define SRAM_CLK		(SPIMEM2 + 0x50u)
#define SPI1_CLOCK		(SPIMEM3 + 0x14u)
#define IOMUX_MSPI		0x500E1200u
#define PSRAM_PIN_BASE		(IOMUX_MSPI + 0x1cu)
#define PSRAM_DQS0_REG		(IOMUX_MSPI + 0x3cu)
#define PSRAM_DQS1_REG		(IOMUX_MSPI + 0x68u)

#define DQS_PHASE_S		1u
#define DQS_PHASE_M		(3u << DQS_PHASE_S)
#define DQS_DELAY90_S		7u
#define DQS_DELAY90_M		(0xFu << DQS_DELAY90_S)
#define DQS_DELAY270_S		17u
#define DQS_DELAY270_M		(0xFu << DQS_DELAY270_S)
#define PIN_DLC_S		4u
#define PIN_DLC_M		(0xFu << PIN_DLC_S)

#define HEX_SYNC_READ		0x0000u
#define HEX_SYNC_WRITE		0x8080u
#define RD_DUMMY		(2u * (14u - 1u))
#define WR_DUMMY		(2u * (7u - 1u))

#define TUNE_ADDR		0x80u
#define TUNE_LEN		128u
#define CHUNK			64u
#define PHASE_NUM		4u
#define DELAY_NUM		31u
#define DELAY_SWEEPS		100u
#define MPLL_MHZ		400u

extern void board_psram_hex_xact(uint32_t cmd, uint32_t addr, uint32_t dummy,
				 uint32_t *tx, uint32_t tx_bits,
				 uint32_t *rx, uint32_t rx_bits);

/*
 * Same reference vector as IDF mspi_timing_by_dqs.c s_test_data[].
 * Bit-pattern dense so a single flipped bit fails the memcmp.
 */
static const uint32_t s_ref[TUNE_LEN / 4u] = {
	0x7f786655u, 0xa5ff005au, 0x3f3c33aau, 0xa5ff5a00u,
	0x1f1e9955u, 0xa5005affu, 0x0f0fccaau, 0xa55a00ffu,
	0x07876655u, 0xffa55a00u, 0x03c333aau, 0xff00a55au,
	0x01e19955u, 0xff005aa5u, 0x00f0ccaau, 0xff5a00a5u,
	0x80786655u, 0x00a5ff5au, 0xc03c33aau, 0x00a55affu,
	0xe01e9355u, 0x00ff5aa5u, 0xf00fccaau, 0x005affa5u,
	0xf8876655u, 0x5aa5ff00u, 0xfcc333aau, 0x5affa500u,
	0xfee19955u, 0x5a00a5ffu, 0x11f0ccaau, 0x5a00ffa5u,
};

/* {data_dl, dqs_dl} — IDF s_test_delayline_config. */
static const uint8_t s_delay_tbl[DELAY_NUM][2] = {
	{0, 15}, {0, 14}, {0, 13}, {0, 12}, {0, 11}, {0, 10}, {0, 9},
	{0, 8}, {0, 7}, {0, 6}, {0, 5}, {0, 4}, {0, 3}, {0, 2}, {0, 1},
	{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0},
	{8, 0}, {9, 0}, {10, 0}, {11, 0}, {12, 0}, {13, 0}, {14, 0},
	{15, 0},
};

static uint8_t g_best_phase;
static uint8_t g_best_data_dl;
static uint8_t g_best_dqs_dl;

static inline void wr(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)(uintptr_t)a = v;
}

static inline uint32_t rd(uint32_t a)
{
	return *(volatile uint32_t *)(uintptr_t)a;
}

static uint32_t pattern_word(uint32_t salt, uint32_t idx)
{
	return 0xA5000000u ^ (salt * 0x11111111u) ^ (idx * 0x00010001u) ^
	       (idx << 16);
}

static void set_bus_mhz(uint32_t mhz)
{
	uint32_t div = MPLL_MHZ / mhz;
	uint32_t bits;

	bits = ((div - 1u) << 0) | ((div / 2u - 1u) << 8) | ((div - 1u) << 16);
	wr(SRAM_CLK, bits);
	wr(SPI1_CLOCK, bits);
}

static void pin_set_dlc(uint32_t reg, uint8_t dl)
{
	uint32_t v = rd(reg);

	v = (v & ~PIN_DLC_M) | (((uint32_t)dl & 0xFu) << PIN_DLC_S);
	wr(reg, v);
}

static void dqs_set_phase(uint32_t reg, uint8_t phase)
{
	uint32_t v = rd(reg);

	v = (v & ~DQS_PHASE_M) | (((uint32_t)phase & 3u) << DQS_PHASE_S);
	wr(reg, v);
}

static void dqs_set_delay(uint32_t reg, uint8_t dl)
{
	uint32_t v = rd(reg);

	v &= ~(DQS_DELAY90_M | DQS_DELAY270_M);
	v |= (((uint32_t)dl & 0xFu) << DQS_DELAY90_S) |
	     (((uint32_t)dl & 0xFu) << DQS_DELAY270_S);
	wr(reg, v);
}

static void apply_phase(uint8_t phase)
{
	dqs_set_phase(PSRAM_DQS0_REG, phase);
	dqs_set_phase(PSRAM_DQS1_REG, phase);
}

static void apply_delayline(uint8_t data_dl, uint8_t dqs_dl)
{
	uint32_t i;

	for (i = 0u; i < 8u; i++)
		pin_set_dlc(PSRAM_PIN_BASE + i * 4u, data_dl);
	for (i = 0u; i < 10u; i++)
		pin_set_dlc(IOMUX_MSPI + 0x40u + i * 4u, data_dl);
	dqs_set_delay(PSRAM_DQS0_REG, dqs_dl);
	dqs_set_delay(PSRAM_DQS1_REG, dqs_dl);
}

static void clear_tuning(void)
{
	apply_phase(0u);
	apply_delayline(0u, 0u);
}

static void copy_bytes(void *dst, const void *src, uint32_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	uint32_t i;

	for (i = 0u; i < n; i++)
		d[i] = s[i];
}

static void zero_bytes(void *dst, uint32_t n)
{
	uint8_t *d = dst;
	uint32_t i;

	for (i = 0u; i < n; i++)
		d[i] = 0u;
}

static int bytes_eq(const void *a, const void *b, uint32_t n)
{
	const uint8_t *x = a;
	const uint8_t *y = b;
	uint32_t i;

	for (i = 0u; i < n; i++) {
		if (x[i] != y[i])
			return 0;
	}
	return 1;
}

static void write_ref(void)
{
	uint32_t off;
	uint32_t n;
	uint32_t buf[CHUNK / 4u];

	for (off = 0u; off < TUNE_LEN; off += CHUNK) {
		n = TUNE_LEN - off;
		if (n > CHUNK)
			n = CHUNK;
		copy_bytes(buf, (const uint8_t *)s_ref + off, n);
		board_psram_hex_xact(HEX_SYNC_WRITE, TUNE_ADDR + off, WR_DUMMY,
				     buf, n * 8u, NULL, 0u);
	}
}

static int read_matches(void)
{
	uint8_t got[TUNE_LEN];
	uint32_t off;
	uint32_t n;
	uint32_t buf[CHUNK / 4u];

	zero_bytes(got, sizeof(got));
	for (off = 0u; off < TUNE_LEN; off += CHUNK) {
		n = TUNE_LEN - off;
		if (n > CHUNK)
			n = CHUNK;
		zero_bytes(buf, sizeof(buf));
		board_psram_hex_xact(HEX_SYNC_READ, TUNE_ADDR + off, RD_DUMMY,
				     NULL, 0u, buf, n * 8u);
		copy_bytes(got + off, buf, n);
	}
	return bytes_eq(got, s_ref, TUNE_LEN);
}

static void find_window(const uint32_t *good, uint32_t n, uint32_t need,
			uint32_t *len_out, uint32_t *end_out)
{
	uint32_t max = 0u;
	uint32_t run = 0u;
	uint32_t end = 0u;
	uint32_t i;

	for (i = 0u; i < n; i++) {
		if (good[i] == need) {
			run++;
		} else {
			if (run > max) {
				max = run;
				end = i - 1u;
			}
			run = 0u;
		}
	}
	if (run > max) {
		max = run;
		end = n - 1u;
	}
	*len_out = max;
	*end_out = end;
}

static void sweep_phase(void)
{
	uint32_t good[PHASE_NUM];
	uint32_t i;
	uint32_t len;
	uint32_t end;
	uint32_t best;

	for (i = 0u; i < PHASE_NUM; i++) {
		apply_phase((uint8_t)i);
		good[i] = (uint32_t)read_matches();
	}
	find_window(good, PHASE_NUM, 1u, &len, &end);
	if (len == 0u)
		best = 0u;
	else
		best = end - len + 1u;
	g_best_phase = (uint8_t)best;
	apply_phase(g_best_phase);
	board_console_printf("ulmk: psram tune phase=%u win=%u\n",
			     (unsigned)g_best_phase, (unsigned)len);
}

static void sweep_delayline(void)
{
	uint32_t good[DELAY_NUM];
	uint32_t i;
	uint32_t t;
	uint32_t hits;
	uint32_t len;
	uint32_t end;
	uint32_t best;

	apply_phase(g_best_phase);
	for (i = 0u; i < DELAY_NUM; i++) {
		apply_delayline(s_delay_tbl[i][0], s_delay_tbl[i][1]);
		hits = 0u;
		for (t = 0u; t < DELAY_SWEEPS; t++) {
			if (read_matches())
				hits++;
		}
		good[i] = hits;
	}
	find_window(good, DELAY_NUM, DELAY_SWEEPS, &len, &end);
	if (len <= 1u)
		best = 0u;
	else
		best = end - len / 2u;
	g_best_data_dl = s_delay_tbl[best][0];
	g_best_dqs_dl = s_delay_tbl[best][1];
	apply_delayline(g_best_data_dl, g_best_dqs_dl);
	board_console_printf("ulmk: psram tune dl data=%u dqs=%u win=%u\n",
			     (unsigned)g_best_data_dl, (unsigned)g_best_dqs_dl,
			     (unsigned)len);
}

int board_psram_timing_tune(void)
{
	board_console_printf("ulmk: psram timing tune\n");

	clear_tuning();
	set_bus_mhz(20u);
	write_ref();
	set_bus_mhz(200u);

	sweep_phase();
	sweep_delayline();

	apply_phase(g_best_phase);
	apply_delayline(g_best_data_dl, g_best_dqs_dl);

	if (!read_matches()) {
		board_console_printf("ulmk: psram tune verify FAIL\n");
		return -1;
	}
	board_console_printf("ulmk: psram tune ok\n");
	return 0;
}

void board_psram_timing_apply(void)
{
	apply_phase(g_best_phase);
	apply_delayline(g_best_data_dl, g_best_dqs_dl);
}

int board_psram_timing_refine_axi(void *base, uint32_t nbytes)
{
	volatile uint32_t *p;
	uint32_t nwords;
	uint32_t good[DELAY_NUM];
	uint32_t i;
	uint32_t t;
	uint32_t w;
	uint32_t hits;
	uint32_t len;
	uint32_t end;
	uint32_t best;
	uint32_t expect;
	uint32_t got;
	int ok;

	if (!base || nbytes < 64u)
		return -1;

	p = (volatile uint32_t *)base;
	nwords = nbytes / sizeof(uint32_t);
	apply_phase(g_best_phase);

	board_console_printf("ulmk: psram axi refine @%08x %uB\n",
			     (unsigned)(uintptr_t)base, (unsigned)nbytes);

	for (i = 0u; i < DELAY_NUM; i++) {
		apply_delayline(s_delay_tbl[i][0], s_delay_tbl[i][1]);
		hits = 0u;
		for (t = 0u; t < 8u; t++) {
			for (w = 0u; w < nwords; w++)
				p[w] = pattern_word(i, w) ^ (t * 0x01010101u);
			__asm__ volatile("fence rw, rw" ::: "memory");
			board_dcache_clean((const void *)base, nbytes);
			board_dcache_invalidate((const void *)base, nbytes);
			ok = 1;
			for (w = 0u; w < nwords; w++) {
				expect = pattern_word(i, w) ^ (t * 0x01010101u);
				got = p[w];
				if (got != expect) {
					ok = 0;
					break;
				}
			}
			if (ok)
				hits++;
		}
		good[i] = hits;
	}

	find_window(good, DELAY_NUM, 8u, &len, &end);
	if (len <= 1u) {
		board_console_printf("ulmk: psram axi refine FAIL win=%u\n",
				     (unsigned)len);
		board_psram_timing_apply();
		return -1;
	}
	best = end - len / 2u;
	g_best_data_dl = s_delay_tbl[best][0];
	g_best_dqs_dl = s_delay_tbl[best][1];
	board_psram_timing_apply();
	board_console_printf("ulmk: psram axi refine dl data=%u dqs=%u win=%u\n",
			     (unsigned)g_best_data_dl, (unsigned)g_best_dqs_dl,
			     (unsigned)len);
	return 0;
}
