/* SPDX-License-Identifier: MIT */
/*
 * smp_tps_pwm — 3-core SMP demo (TC275 Lite).
 *
 *   CPU2  TPS server: IPC service that samples the pot via adc_read
 *   CPU0  client: duty LED1 PWM rises with TPS (throttle body bank A)
 *   CPU1  client: duty LED2 PWM falls with TPS (complementary bank B)
 *
 * Console lines are atomic via board_console_printf (console IPC server).
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
#include "board_console.h"

void board_services_init(const ulmk_boot_info_t *info);
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
static ULMK_PRIVATE volatile uint32_t g_server_ready;

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

	board_console_printf("smp_tps_pwm: TPS server on CPU%u\r\n",
			     ulmk_cpu_id());

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

		board_console_printf(
			"smp_tps_pwm: CPU2 servo raw=%u duty=%u\r\n",
			(uint32_t)raw, reply.words[1]);
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
		board_timer_sleep_us(1000u);

	ep = g_tps_ep;

	board_console_printf(
		"smp_tps_pwm: client on CPU%u -> LED%u %c\r\n",
		cpu, led_ch + 1u,
		led_ch == PWM_CH_LED1 ? '+' : '-');

	for (;;) {
		msg.label = MSG_READ_TPS;
		msg.words[0] = cpu;
		rc = ulmk_ep_call(ep, &msg);
		if (rc != ULMK_OK) {
			board_console_printf(
				"smp_tps_pwm: CPU%u ep_call rc=%u\r\n",
				cpu, (uint32_t)(-rc));
			board_timer_sleep_us(PERIOD_US);
			continue;
		}

		tps_duty = msg.words[1];
		if (tps_duty > 1000u)
			tps_duty = 1000u;

		if (led_ch == PWM_CH_LED1)
			duty = tps_duty;
		else
			duty = 1000u - tps_duty;

		(void)pwm_set_duty((uint8_t)led_ch, duty);

		board_console_printf(
			"smp_tps_pwm: CPU%u req TPS duty=%u set PWM%u=%u\r\n",
			cpu, tps_duty, led_ch, duty);

		board_timer_sleep_us(PERIOD_US);
	}
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;
	ulmk_ep_t ep;

	board_services_init(info);
	g_server_ready = 0u;
	g_tps_ep = ULMK_EP_INVALID;

	board_console_puts("\r\n");
	board_console_puts(
		"ulmk: smp_tps_pwm (CPU2 ADC TPS / CPU0+CPU1 PWM)\r\n");

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
