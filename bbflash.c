#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "rpi.h"

#define S_MOSI 10
#define S_MISO 9
#define S_CLK 11
#define S_CE0 8
#define S_HOLD 25
#define S_WP 24
#define S_D0 S_MOSI
#define S_D1 S_MISO
#define S_D2 S_WP
#define S_D3 S_HOLD
#define F_RESET 27
#define F_DONE 17

enum spi_state {
	SS_UNCONFIGURED = 0,
	SS_SINGLE,
	SS_DUAL_RX,
	SS_DUAL_TX,
	SS_QUAD_RX,
	SS_QUAD_TX,
	SS_HARDWARE,
};

enum spi_type {
	ST_UNCONFIGURED,
	ST_SINGLE,
	ST_DUAL,
	ST_QUAD,
	ST_QPI,
};

struct bb_spi {
	enum spi_state state;
	enum spi_type type;
	int qpi;

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

static void spi_set_state(struct bb_spi *spi, enum spi_state state) {
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

static void spi_pause(void) {
//	usleep(1);
	return;
}

static void spiBegin(struct bb_spi *spi) {
	spi_set_state(spi, SS_SINGLE);
	gpioWrite(spi->pins.cs, 0);
}

static void spiEnd(struct bb_spi *spi) {
	(void)spi;
	gpioWrite(spi->pins.cs, 1);
}

static uint8_t spiXfer(struct bb_spi *spi, uint8_t out) {
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
		spi_pause();
		in |= ((!!gpioRead(spi->pins.miso)) << bit);
		gpioWrite(spi->pins.clk, 0);
		spi_pause();
	}
	return in;
}

static void spiSingleTx(struct bb_spi *spi, uint8_t out) {
	spi_set_state(spi, SS_SINGLE);
	spiXfer(spi, out);
}

static uint8_t spiSingleRx(struct bb_spi *spi) {
	spi_set_state(spi, SS_SINGLE);
	return spiXfer(spi, 0xff);
}

static void spiDualTx(struct bb_spi *spi, uint8_t out) {
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
		spi_pause();
		gpioWrite(spi->pins.clk, 0);
		spi_pause();
	}
}

static void spiQuadTx(struct bb_spi *spi, uint8_t out) {
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
		spi_pause();
		gpioWrite(spi->pins.clk, 0);
		spi_pause();
	}
}

void spiCommand(struct bb_spi *spi, uint8_t cmd) {
	if (spi->qpi)
		spiQuadTx(spi, cmd);
	else
		spiSingleTx(spi, cmd);
}

static uint8_t spiDualRx(struct bb_spi *spi) {
	int bit;
	uint8_t in = 0;

	spi_set_state(spi, SS_QUAD_RX);
	for (bit = 7; bit >= 0; bit -= 2) {
		gpioWrite(spi->pins.clk, 1);
		spi_pause();
		in |= ((!!gpioRead(spi->pins.d0)) << (bit - 1));
		in |= ((!!gpioRead(spi->pins.d1)) << (bit - 0));
		gpioWrite(spi->pins.clk, 0);
		spi_pause();
	}
	return in;
}

static uint8_t spiQuadRx(struct bb_spi *spi) {
	int bit;
	uint8_t in = 0;

	spi_set_state(spi, SS_QUAD_RX);
	for (bit = 7; bit >= 0; bit -= 4) {
		gpioWrite(spi->pins.clk, 1);
		spi_pause();
		in |= ((!!gpioRead(spi->pins.d0)) << (bit - 3));
		in |= ((!!gpioRead(spi->pins.d1)) << (bit - 2));
		in |= ((!!gpioRead(spi->pins.d2)) << (bit - 1));
		in |= ((!!gpioRead(spi->pins.d3)) << (bit - 0));
		gpioWrite(spi->pins.clk, 0);
		spi_pause();
	}
	return in;
}

int spiTx(struct bb_spi *spi, uint8_t word) {
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

uint8_t spiRx(struct bb_spi *spi) {
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

uint8_t spiReadSr(struct bb_spi *spi, int sr) {
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

void spiWriteSr(struct bb_spi *spi, int sr, uint8_t val) {
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

int spiSetType(struct bb_spi *spi, enum spi_type type) {

	if (spi->type == type)
		return 0;

	switch (type) {

	case ST_SINGLE:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
			spi->qpi = 0;
		}
		spi->type = type;
		spi_set_state(spi, SS_SINGLE);
		break;

	case ST_DUAL:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
			spi->qpi = 0;
		}
		spi->type = type;
		spi_set_state(spi, SS_DUAL_TX);
		break;

	case ST_QUAD:
		if (spi->type == ST_QPI) {
			spiBegin(spi);
			spiCommand(spi, 0xff);	// Exit QPI Mode
			spiEnd(spi);
			spi->qpi = 0;
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
		spi->qpi = 1;
		spi->type = type;
		spi_set_state(spi, SS_QUAD_TX);
		break;

	default:
		fprintf(stderr, "Unrecognized SPI type: %d\n", type);
		return 1;
	}
	return 0;
}

int spiRead(struct bb_spi *spi, uint32_t addr, uint8_t *data, unsigned int count) {

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

int spi_wait_for_not_busy(struct bb_spi *spi) {
	uint8_t sr1;
	sr1 = spiReadSr(spi, 1);

	do {
		sr1 = spiReadSr(spi, 1);
	} while (sr1 & (1 << 0));
	return 0;
}

int spiWrite(struct bb_spi *spi, uint32_t addr, const uint8_t *data, unsigned int count) {

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

uint8_t spiReset(struct bb_spi *spi) {
	// XXX You should check the "Ready" bit before doing this!

	spiSetType(spi, ST_SINGLE);

	spiBegin(spi);
	spiSingleTx(spi, 0x66); // "Enable Reset" command
	spiEnd(spi);

	spiBegin(spi);
	spiSingleTx(spi, 0x99); // "Reset Device" command
	spiEnd(spi);

	usleep(30);
	return 0;
}

void fpgaSlaveMode(struct bb_spi *spi) {

	// Set the CS pin to a GPIO, which will let us control it
	gpioSetMode(spi->pins.cs, PI_OUTPUT);

	// Set CS to 0, which will put the FPGA into slave mode
	gpioWrite(spi->pins.cs, 0);

	usleep(10000); // XXX figure out correct sleep length here

	// Bring the FPGA out of reset
	gpioWrite(F_RESET, 1);

	usleep(1200); // 13.2.SPI Slave Configuration Process
}

int spiInit(struct bb_spi *spi) {
	spi->state = SS_UNCONFIGURED;
	spi->type = ST_UNCONFIGURED;
	spi->qpi = 0;

	// Reset the SPI flash, which will return it to SPI mode even
	// if it's in QPI mode.
	spiReset(spi);

	spiSetType(spi, ST_SINGLE);

	return 0;
}


static inline int isprint(int c)
{
	return c > 32 && c < 127;
}

int print_hex_offset(FILE *stream,
                     const void *block, int count, int offset, uint32_t start)
{

	int byte;
	const uint8_t *b = block;

	count += offset;
	b -= offset;
	for ( ; offset < count; offset += 16) {
		fprintf(stream, "%08x", start + offset);

		for (byte = 0; byte < 16; byte++) {
			if (byte == 8)
				fprintf(stream, " ");
			fprintf(stream, " ");
			if (offset + byte < count)
				fprintf(stream, "%02x", b[offset + byte] & 0xff);
			else
				fprintf(stream, "  ");
		}

		fprintf(stream, "  |");
		for (byte = 0; byte < 16 && byte + offset < count; byte++)
			fprintf(stream, "%c", isprint(b[offset + byte]) ?  b[offset + byte] : '.');
		fprintf(stream, "|\r\n");
	}
	return 0;
}

int print_hex(const void *block, int count, uint32_t start)
{
	FILE *stream = stdout;
	return print_hex_offset(stream, block, count, 0, start);
}

int main(int argc, char *argv[])
{
	int result;
	struct bb_spi spi;

	if (gpioInitialise() < 0) {
		fprintf(stderr, "Unable to initialize GPIO\n");
		return 1;
	}

	/* The dance to put the FPGA into programming mode:
	 *     1) Put it into reset (set C_RESET to 0)
	 *     2) Drive CS to 0
	 *     3) Bring it out of reset
	 *     4) Let CS go back to 1
	 *     5) Set HOLD/ on the SPI flash by setting pin 25 to 0
	 * To program the FPGA
	 */

	spi.pins.clk = S_CLK;
	spi.pins.d0 = S_D0;
	spi.pins.d1 = S_D1;
	spi.pins.d2 = S_D2;
	spi.pins.d3 = S_D3;
	spi.pins.miso = S_MISO;
	spi.pins.mosi = S_MOSI;
	spi.pins.hold = S_HOLD;
	spi.pins.wp = S_WP;
	spi.pins.cs   = S_CE0;

	// Have the SPI flash pay attention to us
	gpioWrite(spi.pins.hold, 1);

	// Disable WP
	gpioWrite(spi.pins.wp, 1);

	// Put the FPGA into reset
	gpioSetMode(F_RESET, PI_OUTPUT);
	gpioWrite(F_RESET, 0);

	// Also monitor the C_DONE pin
	gpioSetMode(F_DONE, PI_INPUT);

	// Restart the FPGA in slave mode
	//fpgaSlaveMode();

	result = gpioRead(F_DONE);
	fprintf(stderr, "Reset before running: %d\n", result);

	spiInit(&spi);

	spiSetType(&spi, ST_QPI);

	// Assert CS
	spiBegin(&spi);

	int i;
	fprintf(stderr, "Write:");
	spiCommand(&spi, 0x90);
	spiTx(&spi, 0x00); // A23-16
	spiTx(&spi, 0x00); // A15-8
	spiRx(&spi); // Dummy0
	spiRx(&spi); // Dummy1
	fprintf(stderr, "\nRead:");
	for (i=0; i<16; i++) {
		fprintf(stderr, " 0x%02x", spiRx(&spi));
	}
	fprintf(stderr, "\n");
	spiEnd(&spi);


	uint8_t data[383316];
	{
		memset(data, 0xaa, sizeof(data));
		int fd = open("/tmp/image-gateware+bios+micropython.bin", O_RDONLY);
		if (read(fd, data, sizeof(data)) != sizeof(data)) {
			perror("uanble to read");
			return 1;
		}
		spiWrite(&spi, 0, data, sizeof(data));
	}

	{
		uint8_t page0[256];
		spiRead(&spi, 0, page0, sizeof(page0));
		print_hex(page0, sizeof(page0), 0);
	}
	{
		uint8_t check_data[sizeof(data)];
		spiRead(&spi, 0, check_data, sizeof(check_data));
		size_t j;
		for (j=0; j<sizeof(check_data); j++) {
			if (data[j] != check_data[j]) {
				fprintf(stderr, "check data %d different: %02x vs %02x\n", j, check_data[j], data[j]);
			}
		}
	}

	result = gpioRead(F_DONE);
	fprintf(stderr, "Programming result: %d\n", result);

	// Deassert CS
	gpioWrite(spi.pins.cs, 1);

	// Deassert hold, if set
	gpioWrite(spi.pins.hold, 1);

	// Return the SPI pins to SPI mode, so we can talk to
	// the FPGA normally
	spiSetType(&spi, ST_SINGLE);
	spiInit(&spi);
	spi_set_state(&spi, SS_HARDWARE);

	return 0;
}
