#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include "rpi.h"
#include "spi.h"

#ifdef ERASE_SIZE_32K
#define ERASE_BLOCK_SIZE 32768
#define ERASE_COMMAND 0x52
#else
#ifdef ERASE_SIZE_64K
#define ERASE_BLOCK_SIZE 65536
#define ERASE_COMMAND 0xd8
#else
#define ERASE_BLOCK_SIZE 4096
#define ERASE_COMMAND 0x20
#endif
#endif

enum ff_spi_quirks {
	// There is no separate "Write SR 2" command.  Instead,
	// you must write SR2 after writing SR1
	SQ_SR2_FROM_SR1    = (1 << 0),

	// Don't issue a "Write Enable" command prior to writing
	// a status register
	SQ_SKIP_SR_WEL     = (1 << 1),

	// Security registers are shifted up by 4 bits
	SQ_SECURITY_NYBBLE_SHIFT = (1 << 2),

	// The "QE" bit is in SR1, not SR2
	SQ_QE_IN_SR1 = (1 << 4),

	// There is no separate "Write SR 2" command.  Instead,
	// you must write SR2 after writing SR3
	SQ_SR2_FROM_SR3    = (1 << 5),
};

struct ff_spi {
	enum spi_state state;
	enum spi_type type;
	enum spi_type desired_type;
	struct spi_id id;
	enum ff_spi_quirks quirks;
	int size_override;
	uint8_t unlock_cmd;

	struct {
		int clk;
		int d0;
		int d1;
		int d2;
		int d3;
		int wp;
		int hold;
		int cs;
		int miso;
		int mosi;
	} pins;
};

static void spi_get_id(struct ff_spi *spi);

static struct timeval timer_start(void) {
	struct timeval tv;
	// start timer
	gettimeofday(&tv, NULL);
	return tv;
}

static uint32_t timer_ms_elapsed(struct timeval *t1) {
	struct timeval t2;
	uint32_t ms;
        gettimeofday(&t2, NULL);

	// Convert time to ms
	ms  = (t2.tv_sec - t1->tv_sec) * 1000.0;      // sec to ms
	ms += (t2.tv_usec - t1->tv_usec) / 1000.0;   // us to ms
	return ms;
}

static void spi_set_state(struct ff_spi *spi, enum spi_state state) {
	if (spi->state == state)
		return;

	switch (state) {
	case SS_SINGLE:
		gpioSetMode(spi->pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_OUTPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_OUTPUT);
		gpioSetMode(spi->pins.wp, PI_OUTPUT);
		gpioWrite(spi->pins.clk, 0);
		gpioWrite(spi->pins.cs, 1);
		gpioWrite(spi->pins.mosi, 1);
		gpioWrite(spi->pins.hold, 1);
		gpioWrite(spi->pins.wp, 1);
		break;

	case SS_DUAL_RX:
		gpioSetMode(spi->pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_INPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_OUTPUT);
		gpioSetMode(spi->pins.wp, PI_OUTPUT);
		break;

	case SS_DUAL_TX:
		gpioSetMode(spi->pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_OUTPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_OUTPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_OUTPUT);
		gpioSetMode(spi->pins.wp, PI_OUTPUT);
		break;

	case SS_QUAD_RX:
		gpioSetMode(spi->pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_INPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_INPUT);
		gpioSetMode(spi->pins.wp, PI_INPUT);
		break;

	case SS_QUAD_TX:
		gpioSetMode(spi->pins.clk, PI_OUTPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_OUTPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_OUTPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_OUTPUT);
		gpioSetMode(spi->pins.wp, PI_OUTPUT);
		break;

	case SS_HARDWARE:
		gpioSetMode(spi->pins.clk, PI_INPUT); // CLK
		gpioSetMode(spi->pins.cs, PI_INPUT); // CE0#
		gpioSetMode(spi->pins.mosi, PI_INPUT); // MOSI
		gpioSetMode(spi->pins.miso, PI_INPUT); // MISO
		gpioSetMode(spi->pins.hold, PI_INPUT);
		gpioSetMode(spi->pins.wp, PI_INPUT);
		break;

	default:
		fprintf(stderr, "Unrecognized spi state\n");
		return;
	}

	spi->state = state;
}

void spiPause(struct ff_spi *spi) {
	(void)spi;
//	usleep(1);
	return;
}

void spiBegin(struct ff_spi *spi) {
	spi_set_state(spi, SS_SINGLE);
	if ((spi->type == ST_SINGLE) || (spi->type == ST_DUAL)) {
		gpioWrite(spi->pins.wp, 1);
		gpioWrite(spi->pins.hold, 1);
	}
	gpioWrite(spi->pins.cs, 0);
}

void spiEnd(struct ff_spi *spi) {
	(void)spi;
	gpioWrite(spi->pins.cs, 1);
}

static uint8_t spiXfer(struct ff_spi *spi, uint8_t out) {
	int bit;
	uint8_t in = 0;
	for (bit = 7; bit >= 0; bit--) {
		if (out & (1 << bit)) {
			gpioWrite(spi->pins.mosi, 1);
		}
		else {
			gpioWrite(spi->pins.mosi, 0);
		}
		gpioRead(spi->pins.mosi);
		gpioWrite(spi->pins.clk, 1);
		gpioRead(spi->pins.clk);
		spiPause(spi);
		in |= ((!!gpioRead(spi->pins.miso)) << bit);
		gpioWrite(spi->pins.clk, 0);
		gpioRead(spi->pins.clk);
		spiPause(spi);
	}
	return in;
}

static void spiSingleTx(struct ff_spi *spi, uint8_t out) {
	spi_set_state(spi, SS_SINGLE);
	spiXfer(spi, out);
}

static uint8_t spiSingleRx(struct ff_spi *spi) {
	spi_set_state(spi, SS_SINGLE);
	return spiXfer(spi, 0xff);
}

static void spiDualTx(struct ff_spi *spi, uint8_t out) {
	int bit;
	spi_set_state(spi, SS_DUAL_TX);
	for (bit = 7; bit >= 0; bit -= 2) {
		if (out & (1 << (bit - 1))) {
			gpioWrite(spi->pins.d0, 1);
		}
		else {
			gpioWrite(spi->pins.d0, 0);
		}

		if (out & (1 << (bit - 0))) {
			gpioWrite(spi->pins.d1, 1);
		}
		else {
			gpioWrite(spi->pins.d1, 0);
		}
		gpioWrite(spi->pins.clk, 1);
		spiPause(spi);
		gpioWrite(spi->pins.clk, 0);
		spiPause(spi);
	}
}

static void spiQuadTx(struct ff_spi *spi, uint8_t out) {
	int bit;
	spi_set_state(spi, SS_QUAD_TX);
	for (bit = 7; bit >= 0; bit -= 4) {
		if (out & (1 << (bit - 3))) {
			gpioWrite(spi->pins.d0, 1);
		}
		else {
			gpioWrite(spi->pins.d0, 0);
		}

		if (out & (1 << (bit - 2))) {
			gpioWrite(spi->pins.d1, 1);
		}
		else {
			gpioWrite(spi->pins.d1, 0);
		}

		if (out & (1 << (bit - 1))) {
			gpioWrite(spi->pins.d2, 1);
		}
		else {
			gpioWrite(spi->pins.d2, 0);
		}

		if (out & (1 << (bit - 0))) {
			gpioWrite(spi->pins.d3, 1);
		}
		else {
			gpioWrite(spi->pins.d3, 0);
		}
		gpioWrite(spi->pins.clk, 1);
		spiPause(spi);
		gpioWrite(spi->pins.clk, 0);
		spiPause(spi);
	}
}

static uint8_t spiDualRx(struct ff_spi *spi) {
	int bit;
	uint8_t in = 0;

	spi_set_state(spi, SS_QUAD_RX);
	for (bit = 7; bit >= 0; bit -= 2) {
		gpioWrite(spi->pins.clk, 1);
		spiPause(spi);
		in |= ((!!gpioRead(spi->pins.d0)) << (bit - 1));
		in |= ((!!gpioRead(spi->pins.d1)) << (bit - 0));
		gpioWrite(spi->pins.clk, 0);
		spiPause(spi);
	}
	return in;
}

static uint8_t spiQuadRx(struct ff_spi *spi) {
	int bit;
	uint8_t in = 0;

	spi_set_state(spi, SS_QUAD_RX);
	for (bit = 7; bit >= 0; bit -= 4) {
		gpioWrite(spi->pins.clk, 1);
		spiPause(spi);
		in |= ((!!gpioRead(spi->pins.d0)) << (bit - 3));
		in |= ((!!gpioRead(spi->pins.d1)) << (bit - 2));
		in |= ((!!gpioRead(spi->pins.d2)) << (bit - 1));
		in |= ((!!gpioRead(spi->pins.d3)) << (bit - 0));
		gpioWrite(spi->pins.clk, 0);
		spiPause(spi);
	}
	return in;
}

int spiTx(struct ff_spi *spi, uint8_t word) {
	switch (spi->type) {
	case ST_SINGLE:
		spiSingleTx(spi, word);
		break;
	case ST_DUAL:
		spiDualTx(spi, word);
		break;
	case ST_QUAD:
	case ST_QPI:
		spiQuadTx(spi, word);
		break;
	default:
		return -1;
	}
	return 0;
}

uint8_t spiRx(struct ff_spi *spi) {
	switch (spi->type) {
	case ST_SINGLE:
		return spiSingleRx(spi);
	case ST_DUAL:
		return spiDualRx(spi);
	case ST_QUAD:
	case ST_QPI:
		return spiQuadRx(spi);
	default:
		return 0xff;
	}
}

void spiCommand(struct ff_spi *spi, uint8_t cmd) {
	if (spi->type == ST_QPI)
		spiQuadTx(spi, cmd);
	else
		spiSingleTx(spi, cmd);
}

uint8_t spiCommandRx(struct ff_spi *spi) {
	if (spi->type == ST_QPI)
		return spiQuadRx(spi);
	else
		return spiSingleRx(spi);
}

void spiEnableQuad(struct ff_spi *spi) {
	if (spi->id.manufacturer_id == 0xef) {
		uint8_t val;
		spiBegin(spi);
		spiCommand(spi, 0x35); // Read status register 2
		val = spiCommandRx(spi);
		spiEnd(spi);

		// These bits shouldn't be 1, so if they're 1 then
		// something is broken.
		if ((val & 0xf) == 0xf)
			return;

		// If this bit is set, we're already in QE mode.
		if (val & (1 << 1))
			return;

		val |= (1 << 1);
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x31);
		spiCommand(spi, val);
		spiEnd(spi);
	}

	if (spi->id.manufacturer_id == 0xc8) {
		uint8_t sr1, sr2;

		// The Giga Devices parts don't have the ability to
		// write SR2 directly -- we must also rewrite SR1.

		spiBegin(spi);
		spiCommand(spi, 0x35); // Read status register 2
		sr2 = spiCommandRx(spi);
		spiEnd(spi);

		// If this bit is set, we're already in QE mode.
		if (sr2 & (1 << 1)) {
			return;
		}

		// Check for the "reserved" , "QE", or "LB" bits set,
		// which can indicate something is wrong.
		if (sr2 & 0x0f) {
			fprintf(stderr, "enable quad: SR2 is 0x%02x, which looks suspicious\n", sr2);
			return;
		}

		// Read SR1, which we'll need in order to write both SR2 and SR1
		spiBegin(spi);
		spiCommand(spi, 0x05); // Read status register 1
		sr1 = spiCommandRx(spi);
		spiEnd(spi);

		// Set "QE" Bit
		sr2 |= (1 << 1);

		// Enable writing to the SPI flash (including to the status registers)
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

		// Perform the update
		spiBegin(spi);
		spiCommand(spi, 0x01); // Write status registers
		spiCommand(spi, sr1);  // Write SR1
		spiCommand(spi, sr2);  // Write SR2
		spiEnd(spi);
	}

	return;
}

uint8_t spiReadStatus(struct ff_spi *spi, uint8_t sr) {
	uint8_t val = 0xff;

	spiBegin(spi);
	switch (sr) {
	case 1:
		spiCommand(spi, 0x05);
		val = spiCommandRx(spi);
		break;

	case 2: {
		if (spi->quirks & SQ_SR2_FROM_SR1) {
			spiCommand(spi, 0x05);
			(void) spiCommandRx(spi);
		}
		else if (spi->quirks & SQ_SR2_FROM_SR3) {
			spiCommand(spi, 0x15);
			(void) spiCommandRx(spi);
		}
		else
			spiCommand(spi, 0x35);
		val = spiCommandRx(spi);
		break;
	}

	case 3:
		spiCommand(spi, 0x15);
		val = spiCommandRx(spi);
		break;

	default:
		fprintf(stderr, "unrecognized status register: %d\n", sr);
		break;
	}
	spiEnd(spi);

	return val;
}

void spiUnlockProtection(struct ff_spi *spi)
{
	if (spi->unlock_cmd != NO_UNLOCK_CMD)
	{
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, spi->unlock_cmd);
		spiEnd(spi);
	}
}

void spiWriteSecurity(struct ff_spi *spi, uint8_t sr, uint8_t security[256]) {

	if (spi->quirks & SQ_SECURITY_NYBBLE_SHIFT)
		sr <<= 4;

	spiUnlockProtection(spi);

	spiBegin(spi);
	spiCommand(spi, 0x06);
	spiEnd(spi);

	// erase the register
	spiBegin(spi);
	spiCommand(spi, 0x44);
	spiCommand(spi, 0x00); // A23-16
	spiCommand(spi, sr);   // A15-8
	spiCommand(spi, 0x00); // A0-7
	spiEnd(spi);

	spi_get_id(spi);
	sleep(1);

	// write enable
	spiBegin(spi);
	spiCommand(spi, 0x06);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x42);
	spiCommand(spi, 0x00); // A23-16
	spiCommand(spi, sr);   // A15-8
	spiCommand(spi, 0x00); // A0-7
	int i;
	for (i = 0; i < 256; i++)
		spiCommand(spi, security[i]);
	spiEnd(spi);

	spi_get_id(spi);
}

void spiReadSecurity(struct ff_spi *spi, uint8_t sr, uint8_t security[256]) {
	if (spi->quirks & SQ_SECURITY_NYBBLE_SHIFT)
		sr <<= 4;

	spiBegin(spi);
	spiCommand(spi, 0x48);	// Read security registers
	spiCommand(spi, 0x00);  // A23-16
	spiCommand(spi, sr);    // A15-8
	spiCommand(spi, 0x00);  // A0-7
	int i;
	for (i = 0; i < 256; i++)
		security[i] = spiCommandRx(spi);
	spiEnd(spi);
}

void spiWriteStatus(struct ff_spi *spi, uint8_t sr, uint8_t val) {

//	if (spi->quirks & SQ_SR2_FROM_SR3) {
//		fprintf(stderr, "Can't write status for this chip\n");
//		return;
//	}

	switch (sr) {
	case 1:
		if (!(spi->quirks & SQ_SKIP_SR_WEL)) {
			spiBegin(spi);
			spiCommand(spi, 0x06);
			spiEnd(spi);
		}

		spiBegin(spi);
		spiCommand(spi, 0x50);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x01);
		spiCommand(spi, val);
		spiEnd(spi);
		break;

	case 2: {
		uint8_t sr1 = 0x00;
		uint8_t sr3 = 0x00;
		sr1 = spiReadStatus(spi, 1);
		sr3 = spiReadStatus(spi, 3);

		if (!(spi->quirks & SQ_SKIP_SR_WEL)) {
			spiBegin(spi);
			spiCommand(spi, 0x06);
			spiEnd(spi);
		}


		spiBegin(spi);
		spiCommand(spi, 0x50);
		spiEnd(spi);

		spiBegin(spi);
		if (spi->quirks & SQ_SR2_FROM_SR1) {
			spiCommand(spi, 0x01);
			spiCommand(spi, sr1);
		}
		else if (spi->quirks & SQ_SR2_FROM_SR3) {
			spiCommand(spi, 0x01);
			spiCommand(spi, sr1);
			spiCommand(spi, sr3);
		}
		else
			spiCommand(spi, 0x31);
		spiCommand(spi, val);

		spiEnd(spi);
		break;
	}

	case 3:
		if (!(spi->quirks & SQ_SKIP_SR_WEL)) {
			spiBegin(spi);
			spiCommand(spi, 0x06);
			spiEnd(spi);
		}


		spiBegin(spi);
		spiCommand(spi, 0x50);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x11);
		spiCommand(spi, val);
		spiEnd(spi);
		break;

	default:
		fprintf(stderr, "unrecognized status register: %d\n", sr);
		break;
	}
}

struct spi_id spiId(struct ff_spi *spi) {
	return spi->id;
}

static void spi_decode_id(struct ff_spi *spi) {

	spi->id.manufacturer = "unknown";
	spi->id.model = "unknown";
	spi->id.capacity = "unknown";
	spi->id.bytes = -1; // unknown

	if (spi->id.manufacturer_id == 0xc2) {
		spi->id.manufacturer = "Macronix";
		if ((spi->id.memory_type == 0x28)
		 && (spi->id.memory_size == 0x15)) {
			spi->id.model = "MX25R1635F";
			spi->id.capacity = "16 Mbit";
			spi->id.bytes = 2 * 1024 * 1024;
		}
	}

	if (spi->id.manufacturer_id == 0xc8) {
		spi->id.manufacturer = "Giga Device";
		if ((spi->id.memory_type == 0x40)
		 && (spi->id.memory_size == 0x15)) {
			spi->id.model = "GD25Q16C";
			spi->id.capacity = "16 Mbit";
			spi->id.bytes = 2 * 1024 * 1024;
		}
	}

	if (spi->id.manufacturer_id == 0xef) {
		spi->id.manufacturer = "Winbond";
		if ((spi->id.memory_type == 0x70)
		 && (spi->id.memory_size == 0x18)) {
			spi->id.model = "W25Q128JV";
			spi->id.capacity = "128 Mbit";
			spi->id.bytes = 16 * 1024 * 1024;
		}
	}

	if (spi->id.manufacturer_id == 0x1f) {
		spi->id.manufacturer = "Adesto";
		 if ((spi->id.memory_type == 0x86)
		  && (spi->id.memory_size == 0x01)) {
			spi->id.model = "AT25SF161";
			spi->id.capacity = "16 Mbit";
			spi->id.bytes = 1 * 1024 * 1024;
		}
	}

	return;
}

static void spi_get_id(struct ff_spi *spi) {
	memset(&spi->id, 0xff, sizeof(spi->id));

	spiBegin(spi);
	spiCommand(spi, 0x90);	// Read manufacturer ID
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	spi->id.manufacturer_id = spiCommandRx(spi);
	spi->id.device_id = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x9f);	// Read device id
	spi->id._manufacturer_id = spiCommandRx(spi);
	spi->id.memory_type = spiCommandRx(spi);
	spi->id.memory_size = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0xab);	// Read electronic signature
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	spi->id.signature = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x4b);	// Read unique ID
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	spiCommand(spi, 0x00);  // Dummy byte 4
	spi->id.serial[0] = spiCommandRx(spi);
	spi->id.serial[1] = spiCommandRx(spi);
	spi->id.serial[2] = spiCommandRx(spi);
	spi->id.serial[3] = spiCommandRx(spi);
	spiEnd(spi);

	spi_decode_id(spi);
	return;
}

void spiOverrideSize(struct ff_spi *spi, uint32_t size) {
	spi->size_override = size;

	// If size is 0, re-read the capacity
	if (!size)
		spi_decode_id(spi);
	else
		spi->id.bytes = size;
}

int spiSetType(struct ff_spi *spi, enum spi_type type) {

	if (spi->type == type)
		return 0;

	uint8_t sr_addr = 2;
	if (spi->quirks & SQ_QE_IN_SR1)
		sr_addr = 1;

	switch (type) {

	case ST_SINGLE:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
		}
		spi->type = type;
		spi_set_state(spi, SS_SINGLE);
		break;

	case ST_DUAL:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
		}
		spi->type = type;
		spi_set_state(spi, SS_DUAL_TX);
		break;

	case ST_QUAD:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
		}

		// Enable QE bit
		else if (spi->id.manufacturer_id == 0xc2) {
			uint8_t old_status = spiReadStatus(spi, 1);
			if (old_status != 0xff) {
				if (! (old_status & (1 << 6)))
					spiWriteStatus(spi, 1, old_status | (1 << 6));
			}
		}
		else {
			uint8_t old_status = spiReadStatus(spi, sr_addr);
			if (old_status != 0xff) {
				if (! (old_status & (1 << 1)))
					spiWriteStatus(spi, sr_addr, old_status | (1 << 1));
			}
		}

		spi->type = type;
		spi_set_state(spi, SS_QUAD_TX);
		break;

	case ST_QPI:
		// Enable QE bit
		if (spi->type != ST_QUAD) {
			if (spi->id.manufacturer_id == 0xc2) {
				uint8_t old_status = spiReadStatus(spi, 1);
				if (old_status != 0xff) {
					if (! (old_status & (1 << 6)))
						spiWriteStatus(spi, 1, old_status | (1 << 6));
				}
			}
			else {
				uint8_t old_status = spiReadStatus(spi, sr_addr);
				if (old_status != 0xff) {
					if (! (old_status & (1 << 1)))
						spiWriteStatus(spi, sr_addr, old_status | (1 << 1));
				}
			}
		}

		spiBegin(spi);
		spiCommand(spi, 0x38);		// Enter QPI Mode
		spiEnd(spi);
		spi->type = type;
		spi_set_state(spi, SS_QUAD_TX);
		break;

	default:
		fprintf(stderr, "Unrecognized SPI type: %d\n", type);
		return 1;
	}
	return 0;
}

int spiSetQe(struct ff_spi *spi) {
	uint8_t sr_addr = 2;
	if (spi->quirks & SQ_QE_IN_SR1)
		sr_addr = 1;

        printf("Attempting to set the QE bit...\n");
	if (spi->id.manufacturer_id == 0xc2) {
	  uint8_t old_status = spiReadStatus(spi, 1);
	  if (old_status != 0xff) {
	    if (! (old_status & (1 << 6))) {
	      spiWriteStatus(spi, 1, old_status | (1 << 6));
	      printf("QE bit set\n");
	    } else
	      printf("QE bit already set, doing nothing\n");
	  }
	}
	else {
	  uint8_t old_status = spiReadStatus(spi, sr_addr);
	  if (old_status != 0xff) {
	    if (! (old_status & (1 << 1))) {
	      spiWriteStatus(spi, sr_addr, old_status | (1 << 1));
	      printf("QE bit set\n");
	    } else
	      printf("QE bit already set, doing nothing\n");
	  }
	}

	return 0;
}

int spiRead(struct ff_spi *spi, uint32_t addr, uint8_t *data, unsigned int count) {

	unsigned int i;

	spiBegin(spi);
	switch (spi->type) {
	case ST_SINGLE:
	case ST_QPI:
		spiCommand(spi, 0x0b);
		break;
	case ST_DUAL:
		spiCommand(spi, 0x3b);
		break;
	case ST_QUAD:
		spiCommand(spi, 0x6b);
		break;
	default:
		fprintf(stderr, "unrecognized spi mode\n");
		spiEnd(spi);
		return 1;
	}
	spiCommand(spi, addr >> 16);
	spiCommand(spi, addr >> 8);
	spiCommand(spi, addr >> 0);
	spiCommand(spi, 0x00);
	for (i = 0; i < count; i++) {
		if ((i & 0x3fff) == 0) {
//			printf("\rReading @ %06x / %06x", addr + i, addr + count);
//			fflush(stdout);
		}
		data[i] = spiRx(spi);
	}
//	printf("\rReading @ %06x / %06x Done\n", addr + i, addr + count);

	spiEnd(spi);
	return 0;
}

static int spi_wait_for_not_busy(struct ff_spi *spi, uint32_t timeout_ms) {
	struct timeval tv;
	uint8_t sr1;

	tv = timer_start();
	sr1 = spiReadStatus(spi, 1);

	do {
		if (timer_ms_elapsed(&tv) > timeout_ms) {
			fprintf(stderr, "never went not busy (SR1: 0x%02x)\n", sr1);
			return -1;
		}
		sr1 = spiReadStatus(spi, 1);
	} while (sr1 & (1 << 0));
	return 0;
}

int spiIsBusy(struct ff_spi *spi) {
	return spiReadStatus(spi, 1) & (1 << 0);
}

int spiBeginErase(struct ff_spi *spi, uint32_t erase_addr) {
	// Enable Write-Enable Latch (WEL)
	spiBegin(spi);
	spiCommand(spi, 0x06);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, ERASE_COMMAND);
	spiCommand(spi, erase_addr >> 16);
	spiCommand(spi, erase_addr >> 8);
	spiCommand(spi, erase_addr >> 0);
	spiEnd(spi);
	return 0;
}

int spiBeginWrite(struct ff_spi *spi, uint32_t addr, const void *v_data, unsigned int count) {
	uint8_t write_cmd = 0x02;
	const uint8_t *data = v_data;
	unsigned int i;

	// Enable Write-Enable Latch (WEL)
	spiBegin(spi);
	spiCommand(spi, 0x06);
	spiEnd(spi);

	// uint8_t sr1 = spiReadStatus(spi, 1);
	// if (!(sr1 & (1 << 1)))
	// 	fprintf(stderr, "error: write-enable latch (WEL) not set, write will probably fail\n");

	spiBegin(spi);
	spiCommand(spi, write_cmd);
	spiCommand(spi, addr >> 16);
	spiCommand(spi, addr >> 8);
	spiCommand(spi, addr >> 0);
	for (i = 0; (i < count) && (i < 256); i++)
		spiTx(spi, *data++);
	spiEnd(spi);

	return 0;
}

void spiSwapTxRx(struct ff_spi *spi) {
	int tmp = spi->pins.mosi;
	spi->pins.mosi = spi->pins.miso;
	spi->pins.miso = tmp;
	spiSetType(spi, ST_SINGLE);
	spi->state = SS_UNCONFIGURED;
	spi_set_state(spi, SS_SINGLE);
}

int spiWrite(struct ff_spi *spi, uint32_t addr, const uint8_t *data, unsigned int count, int quiet) {

	unsigned int i;

	if (addr & 0xff) {
		fprintf(stderr, "Error: Target address is not page-aligned to 256 bytes\n");
		return 1;
	}

	// Erase all applicable blocks
	uint32_t erase_addr;
	uint8_t check_bfr[256];
	uint32_t check_byte;
	for (erase_addr = addr; erase_addr < (addr + count); erase_addr += ERASE_BLOCK_SIZE) {
		if (!quiet) {
			printf("\rErasing @ %06x / %06x", erase_addr, addr + count);
			fflush(stdout);
		}

		spiUnlockProtection(spi);

		spiBeginErase(spi, erase_addr);
		spi_wait_for_not_busy(spi, 1000);

		uint32_t check_addr;
		for (check_addr = erase_addr;
		     check_addr < (erase_addr + ERASE_BLOCK_SIZE);
		     check_addr += 256) {
			spiRead(spi, check_addr, check_bfr, sizeof(check_bfr));
			for (check_byte = 0; check_byte < sizeof(check_bfr); check_byte++) {
				if (check_bfr[check_byte] != 0xff) {
					fprintf(stderr, "flash didn't erase @ 0x%08x\n", check_addr);
					return 1;
				}
			}
		}
	}
	if (!quiet)
		printf("  Done\n");

	uint8_t write_cmd;
	switch (spi->type) {
	case ST_DUAL:
		fprintf(stderr, "dual writes are broken -- need to temporarily set SINGLE mode\n");
		return 1;
	case ST_SINGLE:
	case ST_QPI:
		write_cmd = 0x02;
		break;
	case ST_QUAD:
		write_cmd = 0x32;
		break;
	default:
		fprintf(stderr, "unrecognized spi mode\n");
		return 1;
	}

	int total = count;
	while (count) {
		if (!quiet) {
			printf("\rProgramming @ %06x / %06x", addr, total);
			fflush(stdout);
		}
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

		if (!quiet) {
			uint8_t sr1 = spiReadStatus(spi, 1);
			if (!(sr1 & (1 << 1)))
				fprintf(stderr, "error: write-enable latch (WEL) not set, write will probably fail\n");
		}

		spiBegin(spi);
		spiCommand(spi, write_cmd);
		spiCommand(spi, addr >> 16);
		spiCommand(spi, addr >> 8);
		spiCommand(spi, addr >> 0);
		for (i = 0; (i < count) && (i < 256); i++)
			spiTx(spi, *data++);
		spiEnd(spi);
		count -= i;
		addr += i;
		spi_wait_for_not_busy(spi, 1000);
	}
	if (!quiet) {
		printf("\rProgramming @ %06x / %06x", addr, total);
		printf("  Done\n");
	}
	return 0;
}

uint8_t spiReset(struct ff_spi *spi) {
	int i;

	spiSetType(spi, ST_SINGLE);

	// Writing 0xff eight times is equivalent to exiting QPI mode,
	// or if CFM mode is enabled it will terminate CFM and return
	// to idle.
	spiBegin(spi);
	for (i = 0; i < 8; i++)
		spiCommand(spi, 0xff);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0xab);	// Read electronic signature
	spiEnd(spi);

	// XXX You should check the "Ready" bit before doing this!
	return spi_wait_for_not_busy(spi, 1000);
}

static void spi_wait_cs_idle(struct ff_spi *spi, uint32_t max_ticks) {
	uint32_t ticks = 0;
	gpioSetMode(spi->pins.cs, PI_INPUT); // CE0#
	while (!gpioRead(spi->pins.cs)) {
		if (ticks++ > max_ticks) {
			fprintf(stderr, "timed out while waiting for cs to go high!\n");
			fprintf(stderr, "either the device is defective, or it's not\n");
			fprintf(stderr, "connected properly.\n");
		}
	}
	gpioSetMode(spi->pins.cs, PI_OUTPUT); // CE0#
}

int spiInit(struct ff_spi *spi) {
	spi->state = SS_UNCONFIGURED;
	spi->type = ST_UNCONFIGURED;

	spi_set_state(spi, SS_SINGLE);

	// Have the SPI flash pay attention to us
	gpioWrite(spi->pins.hold, 1);

	// Disable WP
	gpioWrite(spi->pins.wp, 1);

	// Wait for CS to be 1, since the bus is shared and there's a pullup.
	spi_wait_cs_idle(spi, 100000);

	// Reset the SPI flash, which will return it to SPI mode even
	// if it's in QPI mode.
	spiReset(spi);

	if (spi_wait_for_not_busy(spi, 1000)) {
		fprintf(stderr, "WARNING: SPI is still busy, communication may fail\n");;
	}

	spiSetType(spi, ST_SINGLE);

	//spiReset(spi);

	spi_get_id(spi);

	spiEnableQuad(spi);
	if (spi->id.manufacturer_id == 0xef)
		spi->quirks |= SQ_SKIP_SR_WEL | SQ_SECURITY_NYBBLE_SHIFT;
	else if (spi->id.manufacturer_id == 0xc2)
		spi->quirks |= SQ_QE_IN_SR1 | SQ_SR2_FROM_SR3;

	return 0;
}

struct ff_spi *spiAlloc(void) {
	struct ff_spi *spi = (struct ff_spi *)malloc(sizeof(struct ff_spi));
	memset(spi, 0, sizeof(*spi));
	return spi;
}

void spiSetPin(struct ff_spi *spi, enum spi_pin pin, int val) {
	switch (pin) {
	case SP_MOSI: spi->pins.mosi = val; break;
        case SP_MISO: spi->pins.miso = val; break;
        case SP_HOLD: spi->pins.hold = val; break;
        case SP_WP: spi->pins.wp = val; break;
        case SP_CS: spi->pins.cs = val; break;
        case SP_CLK: spi->pins.clk = val; break;
        case SP_D0: spi->pins.d0 = val; break;
        case SP_D1: spi->pins.d1 = val; break;
        case SP_D2: spi->pins.d2 = val; break;
        case SP_D3: spi->pins.d3 = val; break;
	default: fprintf(stderr, "unrecognized pin: %d\n", pin); break;
	}
}

void spiSetUnlockCmd(struct  ff_spi *spi, int cmd)
{
	spi->unlock_cmd = cmd;
}

void spiHold(struct ff_spi *spi) {
	spiBegin(spi);
	spiCommand(spi, 0xb9);
	spiEnd(spi);
}
void spiUnhold(struct ff_spi *spi) {
	spiBegin(spi);
	spiCommand(spi, 0xab);
	spiEnd(spi);
}

void spiFree(struct ff_spi **spi) {
	if (!spi)
		return;
	if (!*spi)
		return;

	spiSetType(*spi, ST_SINGLE);
	spi_set_state(*spi, SS_HARDWARE);
	free(*spi);
	*spi = NULL;
}
