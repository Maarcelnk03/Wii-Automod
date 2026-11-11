
/* IOS returns the CSD shifted by one byte with the least significant word first. */
u32 wii_sd_sector_count(void)
{
	u32 csd[4];
	memcpy(csd, __sd0_csd, sizeof(csd));

	if (!sdio_IsInserted() || (__sdio_getstatus() & SDIO_STATUS_CARD_LOCKED) ||
	    (csd[0] & 0x30))
		return 0;

	u32 version = (csd[3] >> 22) & 3;
	if (version == 1) {
		u32 size = ((csd[1] >> 8) & 0x3fffff) + 1;
		return size <= UINT32_MAX / 1024 ? size * 1024 : 0;
	}
	if (version == 0) {
		u32 size = ((csd[2] & 3) << 10) | (csd[1] >> 22);
		u32 multiplier = (csd[1] >> 7) & 7;
		u32 block_size = (csd[2] >> 8) & 15;
		if (block_size >= 9 && block_size <= 11)
			return (size + 1) << (multiplier + block_size - 7);
	}
	return 0;
}
