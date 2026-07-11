#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "target/aurix/aurix.h"
#include "target/aurix/aurix_ocds.h"
#include <flash/common.h>
#include <flash/nor/core.h>
#include <flash/nor/driver.h>
#include <helper/log.h>
#include <target/target.h>

#define SCU_CHIPID		0xF0036140
#define SCU_CHIPID_CHTEC	0xC0

#define TC3XX_HF_STATUS		0xF8040010
#define TC3XX_HF_ERRSR		0xF8040034
#define TC3XX_PHYS_ALIGN	64
#define TC3XX_BUSY_BIT		2

#define TC2XX_HF_STATUS		0xF8002010
#define TC2XX_HF_ERRSR		0xF8002034
#define TC2XX_FLASH_CACHED	0xA0000000
#define TC2XX_CBS_OCNTRL	0xF000047C
#define TC2XX_CBS_ENDINIT_DIS	0xC0
#define TC2XX_PHYS_ALIGN	16
#define TC2XX_BUSY_BIT		2
#define TC2XX_PFPAGE_BIT	21

struct tc3xx_flash_bank {
	bool probed;
	bool tc2xx;
	uint32_t hf_status;
	uint32_t hf_errsr;
	uint32_t phys_align;
	uint32_t busy_bit;
};

static int tc3xx_soc_write_run(struct aurix_ocds *ocds, uint32_t addr,
			       uint32_t data, const char *step)
{
	int err;

	err = aurix_ocds_queue_soc_write_u32(ocds, addr, data);
	if (err) {
		LOG_ERROR("flash %s: queue write 0x%08" PRIx32 " failed", step, addr);
		return err;
	}
	err = aurix_ocds_run(ocds);
	if (err) {
		LOG_ERROR("flash %s: run after write 0x%08" PRIx32 " failed", step, addr);
		return err;
	}
	return ERROR_OK;
}

static int tc3xx_unlock_endinit(struct aurix_ocds *ocds)
{
	return tc3xx_soc_write_run(ocds, TC2XX_CBS_OCNTRL, TC2XX_CBS_ENDINIT_DIS,
				   "endinit");
}

static int tc3xx_prepare_tc2xx(struct aurix_ocds *ocds, struct tc3xx_flash_bank *bank)
{
	if (!bank->tc2xx)
		return ERROR_OK;
	return tc3xx_unlock_endinit(ocds);
}

static int tc3xx_reset_to_read(struct aurix_ocds *ocds)
{
	return tc3xx_soc_write_run(ocds, 0xAF005554, 0xF0, "reset-read");
}

static int tc3xx_clear_status(struct aurix_ocds *ocds)
{
	return tc3xx_soc_write_run(ocds, 0xAF005554, 0xFA, "clear-status");
}

static int tc3xx_flash_addr(struct flash_bank *bank, struct tc3xx_flash_bank *tc3xx_bank,
			     uint32_t offset)
{
	if (tc3xx_bank->tc2xx)
		return TC2XX_FLASH_CACHED + offset;
	return bank->base + offset;
}

static int tc3xx_poll_busy_clear(struct aurix_ocds *ocds, struct tc3xx_flash_bank *bank)
{
	uint32_t flash_busy = 0xFFFFFFFF;
	int err;
	unsigned int tries;

	for (tries = 0; tries < 1000000; tries++) {
		err = aurix_ocds_queue_soc_read_u32(ocds, bank->hf_status, &flash_busy);
		if (err)
			return err;
		err = aurix_ocds_run(ocds);
		if (err)
			return err;

		if (!(flash_busy & (1u << bank->busy_bit)))
			return ERROR_OK;
	}

	LOG_ERROR("flash poll: timeout HF_STATUS=0x%08" PRIx32, flash_busy);
	return ERROR_FLASH_OPERATION_FAILED;
}

static int tc3xx_poll_page_mode(struct aurix_ocds *ocds, struct tc3xx_flash_bank *bank)
{
	uint32_t flash_status = 0;
	int err;
	unsigned int tries;

	for (tries = 0; tries < 1000000; tries++) {
		err = aurix_ocds_queue_soc_read_u32(ocds, bank->hf_status, &flash_status);
		if (err)
			return err;
		err = aurix_ocds_run(ocds);
		if (err)
			return err;

		if (flash_status & (1u << TC2XX_PFPAGE_BIT))
			return ERROR_OK;
	}

	LOG_ERROR("flash poll: page mode timeout HF_STATUS=0x%08" PRIx32, flash_status);
	return ERROR_FLASH_OPERATION_FAILED;
}

static int tc3xx_poll_done(struct aurix_ocds *ocds, struct tc3xx_flash_bank *bank)
{
	if (bank->tc2xx)
		return tc3xx_poll_busy_clear(ocds, bank);

	uint32_t flash_err = 0;
	uint32_t flash_busy = 0xFFFFFFFF;
	int err;
	unsigned int tries;

	for (tries = 0; tries < 1000000; tries++) {
		err = aurix_ocds_queue_soc_read_u32(ocds, bank->hf_errsr, &flash_err);
		if (err)
			return err;
		err = aurix_ocds_queue_soc_read_u32(ocds, bank->hf_status, &flash_busy);
		if (err)
			return err;
		err = aurix_ocds_run(ocds);
		if (err)
			return err;

		if (flash_err)
			break;
		if (!(flash_busy & (1u << bank->busy_bit)))
			return ERROR_OK;
	}

	if (flash_err) {
		LOG_ERROR("flash poll: HF_ERRSR=0x%08" PRIx32 " HF_STATUS=0x%08" PRIx32,
			  flash_err, flash_busy);
		return ERROR_FLASH_OPERATION_FAILED;
	}

	LOG_ERROR("flash poll: timeout HF_STATUS=0x%08" PRIx32, flash_busy);
	return ERROR_FLASH_OPERATION_FAILED;
}

static void tc3xx_setup_family(struct tc3xx_flash_bank *bank, uint32_t chipid)
{
	if ((chipid & SCU_CHIPID_CHTEC) == 0x80) {
		bank->tc2xx = false;
		bank->hf_status = TC3XX_HF_STATUS;
		bank->hf_errsr = TC3XX_HF_ERRSR;
		bank->phys_align = TC3XX_PHYS_ALIGN;
		bank->busy_bit = TC3XX_BUSY_BIT;
		LOG_INFO("tc3xx flash: TC3xx family (CHIPID 0x%08" PRIx32 ")", chipid);
	} else {
		bank->tc2xx = true;
		bank->hf_status = TC2XX_HF_STATUS;
		bank->hf_errsr = TC2XX_HF_ERRSR;
		bank->phys_align = TC2XX_PHYS_ALIGN;
		bank->busy_bit = TC2XX_BUSY_BIT;
		LOG_INFO("tc3xx flash: TC2xx family (CHIPID 0x%08" PRIx32 ")", chipid);
	}
}

static int tc3xx_probe(struct flash_bank *bank)
{
	struct tc3xx_flash_bank *tc3xx_bank = bank->driver_priv;
	uint32_t flash_addr = bank->base;
	uint32_t chipid;
	int retval;

	if (tc3xx_bank->probed)
		return ERROR_OK;

	retval = target_read_u32(bank->target, SCU_CHIPID, &chipid);
	if (retval != ERROR_OK) {
		LOG_ERROR("Cannot read CHIPID register.");
		return retval;
	}

	if (((chipid & SCU_CHIPID_CHTEC) != 0x80) &&
	    ((chipid & SCU_CHIPID_CHTEC) != 0x40) &&
	    ((chipid & SCU_CHIPID_CHTEC) != 0x00)) {
		LOG_ERROR("CHIPID register does not match tc2xx/tc3xx (read 0x%08" PRIx32 ").",
			  chipid);
		return ERROR_FAIL;
	}

	tc3xx_setup_family(tc3xx_bank, chipid);
	LOG_DEBUG("IDCHIP = %08" PRIx32, chipid);

	bank->num_sectors = bank->size / 16 / 1024;
	bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].size = 0x4000;
		bank->sectors[i].offset = flash_addr - bank->base;
		flash_addr += 0x4000;
		bank->sectors[i].is_erased = -1;
		bank->sectors[i].is_protected = -1;
	}

	tc3xx_bank->probed = true;

	return ERROR_OK;
}

static int tc3xx_auto_probe(struct flash_bank *bank)
{
	struct tc3xx_flash_bank *tc3xx_bank = bank->driver_priv;

	if (tc3xx_bank->probed)
		return ERROR_OK;

	return tc3xx_probe(bank);
}

int tc3xx_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct tc3xx_flash_bank *tc3xx_bank = bank->driver_priv;
	struct aurix_ocds *ocds = target_to_aurix(bank->target)->ocds;
	int err;

	while (first <= last) {
		uint32_t addr = tc3xx_flash_addr(bank, tc3xx_bank,
						 bank->sectors[first].offset);
		uint32_t sector_count = MIN(last - first + 1,
					    tc3xx_bank->phys_align - (first % tc3xx_bank->phys_align));

		first += sector_count;

		err = tc3xx_prepare_tc2xx(ocds, tc3xx_bank);
		if (err)
			goto err;

		if (tc3xx_bank->tc2xx) {
			err = tc3xx_clear_status(ocds);
			if (err)
				goto err;
		}

		err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA50, addr);
		if (err)
			goto err;
		err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA58, sector_count);
		if (err)
			goto err;
		err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0x80);
		if (err)
			goto err;
		err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0x50);
		if (err)
			goto err;
		err = aurix_ocds_run(ocds);
		if (err)
			goto err;

		err = tc3xx_poll_done(ocds, tc3xx_bank);
		if (err)
			goto err;
	}

	return ERROR_OK;

err:
	LOG_ERROR("Failed to execute flash erase sequence @ 0x%08" PRIx32,
		  bank->base + bank->sectors[first > 0 ? first - 1 : 0].offset);
	return ERROR_FLASH_OPERATION_FAILED;
}

static int tc3xx_write_page(struct flash_bank *bank, struct aurix_ocds *ocds,
			      const uint8_t *buffer, uint32_t offset,
			      uint32_t page_len)
{
	struct tc3xx_flash_bank *tc3xx_bank = bank->driver_priv;
	uint32_t i;
	int err;

	err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF005554, 0x50);
	if (err)
		return err;
	err = aurix_ocds_run(ocds);
	if (err)
		return err;
	err = tc3xx_poll_busy_clear(ocds, tc3xx_bank);
	if (err)
		return err;

	for (i = 0; i < 32; i += 4) {
		uint32_t data = 0xFFFFFFFF;

		if (i < page_len)
			memcpy(&data, buffer + i, MIN(4u, page_len - i));
		err = aurix_ocds_queue_soc_write_u32(ocds,
				0xAF0055F0 + ((i % 8) == 0 ? 0 : 4), data);
		if (err)
			return err;
	}

	err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA50,
			tc3xx_flash_addr(bank, tc3xx_bank, offset));
	if (err)
		return err;
	err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA58, 0);
	if (err)
		return err;
	err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0xA0);
	if (err)
		return err;
	err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0xAA);
	if (err)
		return err;

	err = aurix_ocds_run(ocds);
	if (err)
		return err;

	return tc3xx_poll_done(ocds, tc3xx_bank);
}

static int tc3xx_write(struct flash_bank *bank, const uint8_t *buffer,
		       uint32_t offset, uint32_t count)
{
	struct tc3xx_flash_bank *tc3xx_bank = bank->driver_priv;
	struct aurix_ocds *ocds = target_to_aurix(bank->target)->ocds;
	uint32_t page_offset = 0;
	int err;

	if (offset & 0x1F)
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;

	if (tc3xx_bank->tc2xx) {
		err = tc3xx_prepare_tc2xx(ocds, tc3xx_bank);
		if (err)
			goto err;
	}

	while (page_offset < count) {
		uint32_t chunk = MIN(32u, count - page_offset);

		if (!tc3xx_bank->tc2xx && (count - page_offset) >= 256) {
			/* TC3xx burst path — keep original batched behaviour */
			uint32_t i;

			err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF005554, 0x50);
			if (err)
				goto err;
			for (i = 0; i < 256; i += 4) {
				uint32_t data;

				memcpy(&data, buffer + page_offset + i, 4);
				err = aurix_ocds_queue_soc_write_u32(
					ocds, 0xAF0055F0 + ((i % 8) == 0 ? 0 : 4), data);
				if (err)
					goto err;
			}
			err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA50,
							     bank->base + offset + page_offset);
			if (err)
				goto err;
			err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AA58, 0);
			if (err)
				goto err;
			err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0xA0);
			if (err)
				goto err;
			err = aurix_ocds_queue_soc_write_u32(ocds, 0xAF00AAA8, 0xA6);
			if (err)
				goto err;
			err = aurix_ocds_run(ocds);
			if (err)
				goto err;
			err = tc3xx_poll_done(ocds, tc3xx_bank);
			if (err)
				goto err;
			page_offset += 256;
			continue;
		}

		err = tc3xx_write_page(bank, ocds, buffer + page_offset,
				       offset + page_offset, chunk);
		if (err)
			goto err;
		page_offset += 32;
	}

	return ERROR_OK;

err:
	LOG_ERROR("Failed to execute flash program sequence");
	return ERROR_FLASH_OPERATION_FAILED;
}

static int tc3xx_read(struct flash_bank *bank, uint8_t *buffer, uint32_t offset,
		      uint32_t count)
{
	return target_read_buffer(bank->target, bank->base + offset, count, buffer);
}

FLASH_BANK_COMMAND_HANDLER(tc3xx_flash_bank_command)
{
	struct tc3xx_flash_bank *tc3xx_bank;

	tc3xx_bank = malloc(sizeof(struct tc3xx_flash_bank));
	if (!tc3xx_bank)
		return ERROR_FLASH_OPERATION_FAILED;

	tc3xx_bank->probed = false;
	tc3xx_bank->tc2xx = false;
	tc3xx_bank->hf_status = TC3XX_HF_STATUS;
	tc3xx_bank->hf_errsr = TC3XX_HF_ERRSR;
	tc3xx_bank->phys_align = TC3XX_PHYS_ALIGN;
	tc3xx_bank->busy_bit = TC3XX_BUSY_BIT;

	bank->driver_priv = tc3xx_bank;

	return ERROR_OK;
}

const struct flash_driver tc3xx_flash = {
	.name = "tc3xx",
	.flash_bank_command = tc3xx_flash_bank_command,
	.probe = tc3xx_probe,
	.auto_probe = tc3xx_auto_probe,
	.erase = tc3xx_erase,
	.write = tc3xx_write,
	.read = tc3xx_read,
};
