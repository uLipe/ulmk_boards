/* SPDX-License-Identifier: MIT */
/*
 * smp_tps_pwm — 3-core SMP demo (TC275 Lite).
 *
 *   CPU2  TPS server: IPC service that samples the pot via adc_read
 *   CPU0  client: duty LED1 PWM rises with TPS (throttle body bank A)
 *   CPU1  client: duty LED2 PWM falls with TPS (complementary bank B)
 *
 * ADC/PWM driver bring-up stays on CPU0 (root); secondaries only use the
 * client APIs.  That avoids driver-init races when affinity is not CPU0.
 *
 * Build: --enable-smp --component smp_tps_pwm
 * UART:  often /dev/ttyUSB1 when USB0 is an ESP adapter.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <adc.h>
#include <pwm.h>
#include "board_config.h"

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void board_console_putc(char c);
void board_timer_sleep_us(uint32_t us);

#define ADC_MOD		0u
#define ADC_CH		0u
#define PWM_MOD		0u
#define PWM_CH_LED1	0u
#define PWM_CH_LED2	1u
#define PWM_FREQ_HZ	1000u
#define ADC_FS		4095u
#define PERIOD_US	100000u	/* 10 Hz clients */
#define MSG_READ_TPS	1u

static ULMK_PRIVATE volatile ulmk_ep_t g_tps_ep;
static ULMK_PRIVATE volatile uint32_t g_print_lock;
static ULMK_PRIVATE volatile uint32_t g_server_ready;

static void print_lock(void)
{
	while (g_print_lock != 0u)
		ulmk_thread_yield();
	g_print_lock = 1u;
	__asm__ volatile("" ::: "memory");
}

static void print_unlock(void)
{
	__asm__ volatile("" ::: "memory");
	g_print_lock = 0u;
}

static void put_u32(uint32_t v)
{
	char buf[10];
	uint32_t i = 0u;
	uint32_t n = v;

	if (n == 0u) {
		board_console_putc('0');
		return;
	}
	while (n > 0u && i < sizeof(buf)) {
		buf[i++] = (char)('0' + (n % 10u));
		n /= 10u;
	}
	while (i > 0u)
		board_console_putc(buf[--i]);
}

static uint32_t raw_to_duty(uint16_t raw)
{
	return ((uint32_t)raw * 1000u) / ADC_FS;
}

static void thr_tps_server(void *arg)
{
	ulmk_ep_t ep = (ulmk_ep_t)(uintptr_t)arg;
	ulmk_msg_t msg;
	ulmk_msg_t reply;
	ulmk_tid_t sender;
	uint16_t raw;
	int rc;

	print_lock();
	board_console_puts("smp_tps_pwm: TPS server on CPU");
	put_u32(ulmk_cpu_id());
	board_console_puts("\r\n");
	print_unlock();

	g_server_ready = 1u;

	for (;;) {
		rc = ulmk_ep_recv(ep, &msg, &sender);
		if (rc != ULMK_OK)
			continue;

		raw = 0u;
		if (msg.label == MSG_READ_TPS) {
			rc = adc_read(ADC_CH, &raw);
			if (rc != ULMK_OK)
				raw = 0u;
		}

		reply.label = MSG_READ_TPS;
		reply.words[0] = (uint32_t)raw;
		reply.words[1] = raw_to_duty(raw);
		reply.words[2] = ulmk_cpu_id();
		(void)ulmk_ep_reply(sender, &reply);

		print_lock();
		board_console_puts("smp_tps_pwm: CPU2 servo raw=");
		put_u32(raw);
		board_console_puts(" duty=");
		put_u32(reply.words[1]);
		board_console_puts("\r\n");
		print_unlock();
	}
}

static void thr_client(void *arg)
{
	uint32_t led_ch = (uint32_t)(uintptr_t)arg;
	ulmk_ep_t ep;
	ulmk_msg_t msg;
	uint32_t duty;
	uint32_t tps_duty;
	int rc;
	uint32_t cpu;

	cpu = ulmk_cpu_id();

	while (g_server_ready == 0u)
		ulmk_thread_yield();

	ep = g_tps_ep;

	print_lock();
	board_console_puts("smp_tps_pwm: client on CPU");
	put_u32(cpu);
	board_console_puts(led_ch == PWM_CH_LED1 ? " -> LED1 +\r\n" : " -> LED2 -\r\n");
	print_unlock();

	for (;;) {
		msg.label = MSG_READ_TPS;
		msg.words[0] = cpu;
		rc = ulmk_ep_call(ep, &msg);
		if (rc != ULMK_OK) {
			print_lock();
			board_console_puts("smp_tps_pwm: CPU");
			put_u32(cpu);
			board_console_puts(" ep_call rc=");
			put_u32((uint32_t)(-rc));
			board_console_puts("\r\n");
			print_unlock();
			board_timer_sleep_us(PERIOD_US);
			continue;
		}

		tps_duty = msg.words[1];
		if (tps_duty > 1000u)
			tps_duty = 1000u;

		/* CPU0: open with TPS; CPU1: complementary close. */
		if (led_ch == PWM_CH_LED1)
			duty = tps_duty;
		else
			duty = 1000u - tps_duty;

		(void)pwm_set_duty((uint8_t)led_ch, duty);

		print_lock();
		board_console_puts("smp_tps_pwm: CPU");
		put_u32(cpu);
		board_console_puts(" req TPS duty=");
		put_u32(tps_duty);
		board_console_puts(" set PWM");
		put_u32(led_ch);
		board_console_puts("=");
		put_u32(duty);
		board_console_puts("\r\n");
		print_unlock();

		board_timer_sleep_us(PERIOD_US);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;
	ulmk_ep_t ep;

	board_services_init(info);
	g_print_lock = 0u;
	g_server_ready = 0u;
	g_tps_ep = ULMK_EP_INVALID;

	board_console_puts("\r\n");
	board_console_puts("ulmk: smp_tps_pwm (CPU2 ADC TPS / CPU0+CPU1 PWM)\r\n");

	if (ulmk_cpu_id() != 0u) {
		board_console_puts("smp_tps_pwm: FAIL root not on CPU0\r\n");
		ulmk_thread_exit();
	}
	if ((uint32_t)ULMK_ARCH_NUM_CPU < 3u) {
		board_console_puts("smp_tps_pwm: FAIL NUM_CPU < 3\r\n");
		ulmk_thread_exit();
	}

	tid = adc_init(ADC_MOD);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_tps_pwm: adc_init failed\r\n");
		ulmk_thread_exit();
	}
	if (adc_config(ADC_CH) != ULMK_OK) {
		board_console_puts("smp_tps_pwm: adc_config failed\r\n");
		ulmk_thread_exit();
	}

	tid = pwm_init(PWM_MOD);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_tps_pwm: pwm_init failed\r\n");
		ulmk_thread_exit();
	}
	if (pwm_config(PWM_CH_LED1, PWM_FREQ_HZ, 0u) != ULMK_OK ||
	    pwm_config(PWM_CH_LED2, PWM_FREQ_HZ, 1000u) != ULMK_OK) {
		board_console_puts("smp_tps_pwm: pwm_config failed\r\n");
		ulmk_thread_exit();
	}
	(void)pwm_enable(PWM_CH_LED1, 1);
	(void)pwm_enable(PWM_CH_LED2, 1);

	ep = ulmk_ep_create();
	if (ep == ULMK_EP_INVALID) {
		board_console_puts("smp_tps_pwm: ep_create failed\r\n");
		ulmk_thread_exit();
	}
	g_tps_ep = ep;

	attr.name       = "tps_srv2";
	attr.entry      = thr_tps_server;
	attr.arg        = (void *)(uintptr_t)ep;
	attr.priority   = 4u;
	attr.stack_size = 3072u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 2u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_tps_pwm: FAIL spawn server\r\n");
		ulmk_thread_exit();
	}
	(void)ulmk_ep_grant(ep, tid);

	attr.name       = "tbi_a0";
	attr.entry      = thr_client;
	attr.arg        = (void *)(uintptr_t)PWM_CH_LED1;
	attr.priority   = 5u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 0u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_tps_pwm: FAIL spawn CPU0 client\r\n");
		ulmk_thread_exit();
	}
	(void)ulmk_ep_grant(ep, tid);

	attr.name       = "tbi_b1";
	attr.entry      = thr_client;
	attr.arg        = (void *)(uintptr_t)PWM_CH_LED2;
	attr.priority   = 5u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 1u;
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("smp_tps_pwm: FAIL spawn CPU1 client\r\n");
		ulmk_thread_exit();
	}
	(void)ulmk_ep_grant(ep, tid);

	board_console_puts("smp_tps_pwm: server+clients spawned\r\n");
	ulmk_thread_exit();
}
