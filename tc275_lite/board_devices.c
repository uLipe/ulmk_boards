/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_can.h>
#include <ulmk_device_pwm.h>
#include <ulmk_device_adc.h>
#include <ulmk_device_gpio.h>
#include "board_devices.h"

static int g_mgr_ready;

int board_devices_mgr_init(void)
{
	int rc;

	if (g_mgr_ready)
		return ULMK_OK;
	rc = ulmk_dev_manager_init();
	if (rc != ULMK_OK)
		return rc;
	g_mgr_ready = 1;
	return ULMK_OK;
}

int board_devices_register_can(uint8_t n, ulmk_ep_t ep, ulmk_tid_t tid)
{
	char path[12];
	int rc;

	if (ep == ULMK_EP_INVALID || tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	if (n > 9u)
		return ULMK_EINVAL;

	rc = board_devices_mgr_init();
	if (rc != ULMK_OK)
		return rc;

	path[0] = '/';
	path[1] = 'd';
	path[2] = 'e';
	path[3] = 'v';
	path[4] = '/';
	path[5] = 'c';
	path[6] = 'a';
	path[7] = 'n';
	path[8] = (char)('0' + n);
	path[9] = '\0';

	return ulmk_dev_register(path, ep, tid, ULMK_DEV_CLASS_CAN, n);
}

int board_devices_register_pwm(ulmk_ep_t ep, ulmk_tid_t tid)
{
	int rc;

	if (ep == ULMK_EP_INVALID || tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;

	rc = board_devices_mgr_init();
	if (rc != ULMK_OK)
		return rc;

	return ulmk_dev_register("/dev/pwm0", ep, tid,
				 ULMK_DEV_CLASS_PWM, 0u);
}

int board_devices_register_adc(ulmk_ep_t ep, ulmk_tid_t tid)
{
	int rc;

	if (ep == ULMK_EP_INVALID || tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;

	rc = board_devices_mgr_init();
	if (rc != ULMK_OK)
		return rc;

	return ulmk_dev_register("/dev/adc0", ep, tid,
				 ULMK_DEV_CLASS_ADC, 0u);
}

int board_devices_register_gpio(uint8_t n, ulmk_ep_t ep, ulmk_tid_t tid)
{
	char path[13];
	int rc;

	if (ep == ULMK_EP_INVALID || tid == ULMK_TID_INVALID)
		return ULMK_EINVAL;
	if (n > 9u)
		return ULMK_EINVAL;

	rc = board_devices_mgr_init();
	if (rc != ULMK_OK)
		return rc;

	path[0] = '/';
	path[1] = 'd';
	path[2] = 'e';
	path[3] = 'v';
	path[4] = '/';
	path[5] = 'g';
	path[6] = 'p';
	path[7] = 'i';
	path[8] = 'o';
	path[9] = (char)('0' + n);
	path[10] = '\0';

	return ulmk_dev_register(path, ep, tid, ULMK_DEV_CLASS_GPIO, n);
}
