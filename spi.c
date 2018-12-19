#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include "rpi.h"
#include "spi.h"

struct ff_spi {
	enum spi_state state;
	enum spi_type type;
	enum spi_type desired_type;

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
		gpioSetMode(spi->pins.clk, PI_ALT0); // CLK
		gpioSetMode(spi->pins.cs, PI_ALT0); // CE0#
		gpioSetMode(spi->pins.mosi, PI_ALT0); // MOSI
		gpioSetMode(spi->pins.miso, PI_ALT0); // MISO
		gpioSetMode(spi->pins.hold, PI_OUTPUT);
		gpioSetMode(spi->pins.wp, PI_OUTPUT);
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
		gpioWrite(spi->pins.clk, 1);
		spiPause(spi);
		in |= ((!!gpioRead(spi->pins.miso)) << bit);
		gpioWrite(spi->pins.clk, 0);
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

uint8_t spiReadSr(struct ff_spi *spi, int sr) {
	uint8_t val = 0xff;

	switch (sr) {
	case 1:
		spiBegin(spi);
		spiCommand(spi, 0x05);
		val = spiRx(spi);
		spiEnd(spi);
		break;

	case 2:
		spiBegin(spi);
		spiCommand(spi, 0x35);
		val = spiRx(spi);
		spiEnd(spi);
		break;

	case 3:
		spiBegin(spi);
		spiCommand(spi, 0x15);
		val = spiRx(spi);
		spiEnd(spi);
		break;

	default:
		fprintf(stderr, "unrecognized status register: %d\n", sr);
		break;
	}

	return val;
}

void spiWriteSr(struct ff_spi *spi, int sr, uint8_t val) {
	switch (sr) {
	case 1:
		spiBegin(spi);
		spiCommand(spi, 0x50);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x01);
		spiCommand(spi, val);
		spiEnd(spi);
		break;

	case 2:
		spiBegin(spi);
		spiCommand(spi, 0x50);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x31);
		spiCommand(spi, val);
		spiEnd(spi);
		break;

	case 3:
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
	struct spi_id id;
	memset(&id, 0xff, sizeof(id));

	spiBegin(spi);
	spiCommand(spi, 0x90);	// Read manufacturer ID
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	id.manufacturer_id = spiCommandRx(spi);
	id.device_id = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x9f);	// Read device id
	id._manufacturer_id = spiCommandRx(spi);
	id.memory_type = spiCommandRx(spi);
	id.memory_size = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0xab);	// Read electronic signature
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	id.signature = spiCommandRx(spi);
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x4b);	// Read unique ID
	spiCommand(spi, 0x00);  // Dummy byte 1
	spiCommand(spi, 0x00);  // Dummy byte 2
	spiCommand(spi, 0x00);  // Dummy byte 3
	spiCommand(spi, 0x00);  // Dummy byte 4
	id.serial[0] = spiCommandRx(spi);
	id.serial[1] = spiCommandRx(spi);
	id.serial[2] = spiCommandRx(spi);
	id.serial[3] = spiCommandRx(spi);
	spiEnd(spi);

	return id;
}

int spiSetType(struct ff_spi *spi, enum spi_type type) {

	if (spi->type == type)
		return 0;

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
		spiWriteSr(spi, 2, spiReadSr(spi, 2) | (1 << 1));

		spi->type = type;
		spi_set_state(spi, SS_QUAD_TX);
		break;

	case ST_QPI:
		// Enable QE bit
		spiWriteSr(spi, 2, spiReadSr(spi, 2) | (1 << 1));

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
	for (i = 0; i < count; i++)
		data[i] = spiRx(spi);

	spiEnd(spi);
	return 0;
}

static int spi_wait_for_not_busy(struct ff_spi *spi) {
	uint8_t sr1;
	sr1 = spiReadSr(spi, 1);

	do {
		sr1 = spiReadSr(spi, 1);
	} while (sr1 & (1 << 0));
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

int spiWrite(struct ff_spi *spi, uint32_t addr, const uint8_t *data, unsigned int count) {

	unsigned int i;

	if (addr & 0xff) {
		fprintf(stderr, "Error: Target address is not page-aligned to 256 bytes\n");
		return 1;
	}

	// Erase all applicable blocks
	uint32_t erase_addr;
	for (erase_addr = 0; erase_addr < count; erase_addr += 32768) {
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

		spiBegin(spi);
		spiCommand(spi, 0x52);
		spiCommand(spi, erase_addr >> 16);
		spiCommand(spi, erase_addr >> 8);
		spiCommand(spi, erase_addr >> 0);
		spiEnd(spi);

		spi_wait_for_not_busy(spi);
	}

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

	while (count) {
		spiBegin(spi);
		spiCommand(spi, 0x06);
		spiEnd(spi);

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
		spi_wait_for_not_busy(spi);
	}
	return 0;
}

uint8_t spiReset(struct ff_spi *spi) {
	// XXX You should check the "Ready" bit before doing this!

	// Shift to QPI mode, then back to Single mode, to ensure
	// we're actually in Single mode.
	spiSetType(spi, ST_QPI);
	spiSetType(spi, ST_SINGLE);

	spiBegin(spi);
	spiCommand(spi, 0x66); // "Enable Reset" command
	spiEnd(spi);

	spiBegin(spi);
	spiCommand(spi, 0x99); // "Reset Device" command
	spiEnd(spi);

	usleep(30);
	return 0;
}

int spiInit(struct ff_spi *spi) {
	spi->state = SS_UNCONFIGURED;
	spi->type = ST_UNCONFIGURED;

	// Reset the SPI flash, which will return it to SPI mode even
	// if it's in QPI mode.
	spiReset(spi);

	spiSetType(spi, ST_SINGLE);

	// Have the SPI flash pay attention to us
	gpioWrite(spi->pins.hold, 1);
	// Disable WP
	gpioWrite(spi->pins.wp, 1);

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

        spi_set_state(*spi, SS_HARDWARE);
	free(*spi);
	*spi = NULL;
}