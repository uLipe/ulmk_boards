/* SPDX-License-Identifier: MIT */
/*
 * LTDC RGB565 display server — double-buffer in SDRAM, flip on line IRQ.
 * Timing/pinmux from dts/zephyr.dts (linum_dev panel 1024×600).
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include "board_config.h"
#include "board_ll.h"
#include "display_internal.h"
#include "pinmux_internal.h"

#define DISPLAY_STACK_SIZE	4096u
#define LTDC_AF			14u
#define LTDC_PF_RGB565		2u
#define LTDC_NOTIF_MASK		(1u << DISPLAY_NOTIF_VSYNC)

/* Panel timings (Zephyr linum_dev display-timings, all signals active-low). */
#define LTDC_HSYNC		0u
#define LTDC_VSYNC		0u
#define LTDC_HBP		160u
#define LTDC_VBP		23u
#define LTDC_HFP		160u
#define LTDC_VFP		12u
#define LTDC_ACC_HBP		(LTDC_HBP + LTDC_HSYNC)
#define LTDC_ACC_VBP		(LTDC_VBP + LTDC_VSYNC)
#define LTDC_ACC_ACTIVE_W	(LTDC_ACC_HBP + DISPLAY_W)
#define LTDC_ACC_ACTIVE_H	(LTDC_ACC_VBP + DISPLAY_H)
#define LTDC_TOTAL_W		(LTDC_ACC_ACTIVE_W + LTDC_HFP - 1u)
#define LTDC_TOTAL_H		(LTDC_ACC_ACTIVE_H + LTDC_VFP - 1u)

struct ltdc_pin {
	uint8_t port;
	uint8_t pin;
};

static const struct ltdc_pin g_ltdc_pins[] = {
	{ 8u, 9u }, { 8u, 10u }, { 8u, 14u }, { 8u, 15u },
	{ 9u, 0u }, { 9u, 1u }, { 9u, 2u }, { 9u, 3u },
	{ 9u, 4u }, { 9u, 5u }, { 9u, 6u }, { 9u, 7u },
	{ 9u, 8u }, { 9u, 9u }, { 9u, 10u }, { 9u, 11u },
	{ 9u, 12u }, { 9u, 13u }, { 9u, 14u }, { 9u, 15u },
	{ 10u, 0u }, { 10u, 1u }, { 10u, 2u },
	{ 10u, 3u }, { 10u, 4u }, { 10u, 5u }, { 10u, 6u },
	{ 10u, 7u },
};

ulmk_ep_t g_display_ep;
static LTDC_TypeDef *g_ltdc __attribute__((section(".user_bss")));
static ulmk_notif_t g_vsync_notif __attribute__((section(".user_bss")));
static uint8_t g_front __attribute__((section(".user_bss")));
static uint8_t g_back __attribute__((section(".user_bss")));
static uint32_t g_pend_addr __attribute__((section(".user_bss")));
static int g_ltdc_ready __attribute__((section(".user_bss")));

static void ltdc_pinmux(void)
{
	pinmux_cfg_t cfg;
	uint32_t i;

	cfg.dir = PINMUX_DIR_OUT;
	cfg.pull = PINMUX_PULL_NONE;
	cfg.alt = LTDC_AF;
	cfg.flags = 0u;
	for (i = 0u; i < (sizeof(g_ltdc_pins) / sizeof(g_ltdc_pins[0])); i++) {
		cfg.port = g_ltdc_pins[i].port;
		cfg.pin = g_ltdc_pins[i].pin;
		(void)pinmux_apply(&cfg);
	}
}

static void disp_on_gpio(int on)
{
	GPIO_TypeDef *gpio;
	uint32_t pin;

	gpio = pinmux_gpio(ULMK_BOARD_DISP_ON_PORT);
	if (!gpio)
		return;
	pin = 1u << ULMK_BOARD_DISP_ON_PIN;
	LL_GPIO_SetPinMode(gpio, pin, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(gpio, pin, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinPull(gpio, pin, LL_GPIO_PULL_NO);
	if (on)
		LL_GPIO_SetOutputPin(gpio, pin);
	else
		LL_GPIO_ResetOutputPin(gpio, pin);
}

static void ltdc_pll3_init(void)
{
	volatile uint32_t guard = 1000000u;

	if (LL_RCC_PLL3_IsReady())
		return;

	LL_RCC_PLL3_Disable();
	guard = 1000000u;
	while (LL_RCC_PLL3_IsReady() && guard-- > 0u)
		;

	LL_RCC_PLL3_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
	LL_RCC_PLL3_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
	LL_RCC_PLL3_SetM(5u);
	LL_RCC_PLL3_SetN(128u);
	LL_RCC_PLL3_SetP(4u);
	LL_RCC_PLL3_SetQ(4u);
	LL_RCC_PLL3_SetR(25u);
	LL_RCC_PLL3R_Enable();
	LL_RCC_PLL3_Enable();
	guard = 1000000u;
	while (!LL_RCC_PLL3_IsReady() && guard-- > 0u)
		;
}

static void ltdc_layer1_cfg(uint32_t fb_addr)
{
	uint32_t pitch;
	uint32_t line_len;
	uint32_t hstart;
	uint32_t hstop;
	uint32_t vstart;
	uint32_t vstop;
	uint32_t ahbp;
	uint32_t avbp;

	/*
	 * H7 CFBLL is (active_width_bytes + 7), not +3 (F4/U5).  Wrong
	 * constant shears every scanline — classic diagonal "streak" text.
	 */
	pitch = DISPLAY_W * DISPLAY_BPP;
	line_len = pitch + 7u;

	ahbp = (g_ltdc->BPCR & LTDC_BPCR_AHBP_Msk) >> LTDC_BPCR_AHBP_Pos;
	avbp = (g_ltdc->BPCR & LTDC_BPCR_AVBP_Msk) >> LTDC_BPCR_AVBP_Pos;
	/* HAL: WHSTPOS = X0+AHBP+1, WHSPPOS = X1+AHBP (X1 = X0+width). */
	hstart = ahbp + 1u;
	hstop = DISPLAY_W + ahbp;
	vstart = avbp + 1u;
	vstop = DISPLAY_H + avbp;

	LTDC_Layer1->WHPCR = (hstop << LTDC_LxWHPCR_WHSPPOS_Pos) |
			     (hstart << LTDC_LxWHPCR_WHSTPOS_Pos);
	LTDC_Layer1->WVPCR = (vstop << LTDC_LxWVPCR_WVSPPOS_Pos) |
			     (vstart << LTDC_LxWVPCR_WVSTPOS_Pos);
	LTDC_Layer1->PFCR = LTDC_PF_RGB565;
	LTDC_Layer1->CFBAR = fb_addr;
	LTDC_Layer1->CFBLR = (pitch << LTDC_LxCFBLR_CFBP_Pos) |
			     (line_len << LTDC_LxCFBLR_CFBLL_Pos);
	LTDC_Layer1->CFBLNR = DISPLAY_H;
	LTDC_Layer1->CACR = 0xFFu;
	LTDC_Layer1->DCCR = 0u;
	LTDC_Layer1->BFCR = (5u << 8) | 6u;
	LTDC_Layer1->CR = LTDC_LxCR_LEN;
}

static int ltdc_hw_init(void)
{
	uint32_t fb0;

	if (g_ltdc_ready)
		return ULMK_OK;

	ltdc_pll3_init();
	LL_APB3_GRP1_EnableClock(LL_APB3_GRP1_PERIPH_LTDC);

	g_ltdc->GCR = 0u;
	LTDC_Layer2->CR = 0u;

	g_ltdc->SSCR = (LTDC_HSYNC << LTDC_SSCR_HSW_Pos) |
		       (LTDC_VSYNC << LTDC_SSCR_VSH_Pos);
	g_ltdc->BPCR = (LTDC_ACC_HBP << LTDC_BPCR_AHBP_Pos) |
		       (LTDC_ACC_VBP << LTDC_BPCR_AVBP_Pos);
	g_ltdc->AWCR = (LTDC_ACC_ACTIVE_W << LTDC_AWCR_AAW_Pos) |
		       (LTDC_ACC_ACTIVE_H << LTDC_AWCR_AAH_Pos);
	g_ltdc->TWCR = (LTDC_TOTAL_W << LTDC_TWCR_TOTALW_Pos) |
		       (LTDC_TOTAL_H << LTDC_TWCR_TOTALH_Pos);
	g_ltdc->BCCR = 0x00FFFFFFu;
	g_ltdc->IER = 0u;
	g_ltdc->LIPCR = 0u;

	g_front = 0u;
	g_back = 1u;
	fb0 = ULMK_BOARD_SDRAM_BASE;
	/* Clear both framebuffers before LTDC starts scanning. */
	{
		volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)fb0;
		uint32_t n;
		uint32_t words = (DISPLAY_FB_BYTES / sizeof(uint16_t)) * 2u;

		for (n = 0u; n < words; n++)
			p[n] = 0u;
		__asm__ volatile("dsb" ::: "memory");
	}
	ltdc_layer1_cfg(fb0);
	g_ltdc->SRCR = LTDC_SRCR_IMR;
	g_ltdc->GCR = LTDC_GCR_LTDCEN;
	g_ltdc_ready = 1;
	return ULMK_OK;
}

static uint32_t fb_phys(uint8_t idx)
{
	return ULMK_BOARD_SDRAM_BASE + (uint32_t)idx * DISPLAY_FB_BYTES;
}

static int display_wait_vsync(void)
{
	uint32_t bits;
	int ret;

	g_ltdc->ICR = LTDC_ICR_CLIF;
	ulmk_irq_ack(ULMK_BOARD_IRQ_LTDC);
	g_ltdc->IER |= LTDC_IER_LIE;
	ret = ulmk_notif_wait(g_vsync_notif, LTDC_NOTIF_MASK, &bits);
	g_ltdc->IER &= ~LTDC_IER_LIE;
	if (ret != ULMK_OK)
		return ret;
	if ((g_ltdc->ISR & LTDC_ISR_LIF) == 0u)
		return ULMK_ETIMEOUT;
	g_ltdc->ICR = LTDC_ICR_CLIF;
	ulmk_irq_ack(ULMK_BOARD_IRQ_LTDC);
	return ULMK_OK;
}

static int display_do_present(uint32_t phys)
{
	int ret;
	uint8_t idx;

	if (!g_ltdc_ready)
		return ULMK_EINVAL;
	if (phys == fb_phys(0u))
		idx = 0u;
	else if (phys == fb_phys(1u))
		idx = 1u;
	else
		return ULMK_EINVAL;

	/*
	 * Program shadow CFBAR and reload on next vertical blank, then wait
	 * on the line IRQ (notif) — never spin on SRCR.VBR.
	 * Caller must D-cache-clean the FB (or dirty rect) before present.
	 */
	LTDC_Layer1->CFBAR = phys;
	g_ltdc->SRCR = LTDC_SRCR_VBR;
	__asm__ volatile("dsb" ::: "memory");
	ret = display_wait_vsync();
	if (ret != ULMK_OK)
		return ret;

	g_front = idx;
	g_back = (idx == 0u) ? 1u : 0u;
	g_pend_addr = phys;
	return ULMK_OK;
}

static int display_do_flip(void)
{
	return display_do_present(fb_phys(g_back));
}

static void display_server(void *arg)
{
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;

	(void)arg;
	g_ltdc = (LTDC_TypeDef *)ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_LTDC_BASE,
					      ULMK_BOARD_LTDC_MAP_SIZE,
					      ULMK_PERM_READ | ULMK_PERM_WRITE,
					      ULMK_MMAP_PERIPH);
	if (!g_ltdc)
		return;

	g_vsync_notif = ulmk_notif_create();
	if (g_vsync_notif == ULMK_NOTIF_INVALID)
		return;
	if (ulmk_irq_bind_hw(ULMK_BOARD_IRQ_LTDC, g_vsync_notif,
			     DISPLAY_NOTIF_VSYNC,
			     ULMK_BOARD_NVIC_SRC(ULMK_BOARD_NVIC_LTDC)) != ULMK_OK)
		return;
	if (ulmk_irq_enable(ULMK_BOARD_IRQ_LTDC) != ULMK_OK)
		return;

	ltdc_pinmux();
	disp_on_gpio(1);

	/* SDRAM FB window — Normal attrs via MAP_SHARED (LTDC DMA master). */
	if (!ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
			  ULMK_BOARD_SDRAM_SIZE,
			  ULMK_PERM_READ | ULMK_PERM_WRITE,
			  ULMK_MMAP_SHARED))
		return;

	(void)ltdc_hw_init();

	for (;;) {
		if (ulmk_ep_recv(g_display_ep, &msg, &sender) != ULMK_OK)
			continue;
		reply.label = 0u;
		reply.words[0] = (uint32_t)ULMK_EINVAL;
		reply.words[1] = 0u;

		switch (msg.label) {
		case DISPLAY_MSG_WRITE:
			if (g_ltdc_ready) {
				reply.words[0] = (uint32_t)ULMK_OK;
				reply.words[1] = fb_phys(g_back);
			}
			break;
		case DISPLAY_MSG_FLIP:
			reply.words[0] = (uint32_t)display_do_flip();
			break;
		case DISPLAY_MSG_ON:
			disp_on_gpio(msg.words[0] != 0u);
			if (msg.words[0] != 0u)
				(void)ltdc_hw_init();
			reply.words[0] = (uint32_t)ULMK_OK;
			break;
		case DISPLAY_MSG_PRESENT:
			reply.words[0] =
				(uint32_t)display_do_present(msg.words[1]);
			break;
		default:
			break;
		}
		ulmk_ep_reply(sender, &reply);
	}
}

ulmk_ep_t display_ep(void)
{
	return g_display_ep;
}

ulmk_tid_t display_init(uint8_t mod)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;

	if (mod != 0u || g_display_ep != ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	g_display_ep = ulmk_ep_create();
	if (g_display_ep == ULMK_EP_INVALID)
		return ULMK_TID_INVALID;
	attr.name = "display";
	attr.entry = display_server;
	attr.priority = 2u;
	attr.stack_size = DISPLAY_STACK_SIZE;
	attr.privilege = ULMK_PRIV_DRIVER;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID)
		return ULMK_TID_INVALID;
	ulmk_cap_grant(tid, ULMK_CAP_MAP_PERIPH);
	ulmk_cap_grant(tid, ULMK_CAP_MAP_SHARED);
	ulmk_cap_grant(tid, ULMK_CAP_IRQ);
	return tid;
}
