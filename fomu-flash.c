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
#include "fpga.h"

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

enum op {
	OP_SPI_READ,
	OP_SPI_WRITE,
	OP_SPI_VERIFY,
	OP_SPI_PEEK,
	OP_FPGA_BOOT,
	OP_FPGA_RESET,
	OP_UNKNOWN,
};

int print_help(FILE *stream, const char *progname) {
	fprintf(stream, "Fomu Raspberry Pi Flash Utilities\n");
	fprintf(stream, "Usage:\n");
	fprintf(stream, "    %s [-h] [-r] [-p offset] [-f bin] [-w bin] [-v bin] [-s out] [-2 pin] [-3 pin]\n", progname);
	fprintf(stream, "Flags:\n");
	fprintf(stream, "    -h        This help page\n");
	fprintf(stream, "    -r        Reset the FPGA and have it boot from SPI\n");
	fprintf(stream, "    -p offset Peek at 256 bytes of SPI flash at the specified offset\n");
	fprintf(stream, "    -f bin    Load this binary directly into the FPGA\n");
	fprintf(stream, "    -w bin    Write this binary into the SPI flash chip\n");
	fprintf(stream, "    -v bin    Verify the SPI flash contains this data\n");
	fprintf(stream, "    -s out    Save the SPI flash contents to this file\n");
	fprintf(stream, "    -2 pin    Use this pin number as QSPI IO2\n");
	fprintf(stream, "    -3 pin    Use this pin number as QSPI IO3\n");
	return 0;
}

int main(int argc, char **argv) {
	int opt;
	int fd;
	char *op_filename = NULL;
	struct ff_spi *spi;
	struct ff_fpga *fpga;
	int peek_offset = 0;
	enum op op = OP_UNKNOWN;

 	spi = spiAlloc();
	fpga = fpgaAlloc();

	spiSetPin(spi, SP_CLK, S_CLK);
	spiSetPin(spi, SP_D0, S_D0);
	spiSetPin(spi, SP_D1, S_D1);
	spiSetPin(spi, SP_D2, S_D2);
	spiSetPin(spi, SP_D3, S_D3);
	spiSetPin(spi, SP_MISO, S_MISO);
	spiSetPin(spi, SP_MOSI, S_MOSI);
	spiSetPin(spi, SP_HOLD, S_HOLD);
	spiSetPin(spi, SP_WP, S_WP);
	spiSetPin(spi, SP_CS, S_CE0);

	fpgaSetPin(fpga, FP_RESET, F_RESET);
	fpgaSetPin(fpga, FP_DONE, F_DONE);
	fpgaSetPin(fpga, FP_CS, S_CE0);

	if (gpioInitialise() < 0) {
		fprintf(stderr, "Unable to initialize GPIO\n");
		return 1;
	}

	while ((opt = getopt(argc, argv, "hp:rf:w:s:2:3:v:")) != -1) {
		switch (opt) {
		case '2':
			spiSetPin(spi, SP_D2, strtoul(optarg, NULL, 0));
			break;

		case '3':
			spiSetPin(spi, SP_D3, strtoul(optarg, NULL, 0));
			break;

		case 'r':
			op = OP_FPGA_RESET;
			break;

		case 'p':
			op = OP_SPI_PEEK;
			peek_offset = strtoul(optarg, NULL, 0);
			break;

		case 'f':
			op = OP_FPGA_BOOT;
			if (op_filename)
				free(op_filename);
			op_filename = strdup(optarg);
			break;

		case 'w':
			op = OP_SPI_WRITE;
			if (op_filename)
				free(op_filename);
			op_filename = strdup(optarg);
			break;

		case 'v':
			op = OP_SPI_VERIFY;
			if (op_filename)
				free(op_filename);
			op_filename = strdup(optarg);
			break;

		case 's':
			op = OP_SPI_READ;
			if (op_filename)
				free(op_filename);
			op_filename = strdup(optarg);
			break;

		default:
			print_help(stdout, argv[0]);
			return 1;
		}
	}

	if (op == OP_UNKNOWN) {
		print_help(stdout, argv[0]);
		return 1;
	}

	spiInit(spi);
	fpgaInit(fpga);

	switch (op) {
	case OP_SPI_READ: {
		spiSetType(spi, ST_QPI);
		fpgaReset(fpga);
		fd = open(op_filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
		if (fd == -1) {
			perror("unable to open output file");
			break;
		}
		uint8_t *bfr = malloc(16777216);
		spiRead(spi, 0, bfr, 16777216);
		if (write(fd, bfr, 16777216) != 16777216) {
			perror("unable to write SPI flash image");
			break;
		}
		close(fd);
		free(bfr);
		break;
	}

	case OP_SPI_WRITE: {
		spiSetType(spi, ST_QPI);
		fpgaReset(fpga);
		fd = open(op_filename, O_RDONLY);
		if (fd == -1) {
			perror("unable to open input file");
			break;
		}
		struct stat stat;
		if (fstat(fd, &stat) == -1) {
			perror("unable to get bitstream file size");
			break;
		}

		uint8_t *bfr = malloc(stat.st_size);
		if (!bfr) {
			perror("unable to alloc memory for buffer");
			break;
		}
		if (read(fd, bfr, stat.st_size) != stat.st_size) {
			perror("unable to read from file");
			free(bfr);
			break;
		}
		close(fd);
		spiWrite(spi, 0, bfr, stat.st_size);
		break;
	}

	case OP_SPI_VERIFY: {

		spiSetType(spi, ST_QPI);
		fpgaReset(fpga);
		fd = open(op_filename, O_RDONLY);
		if (fd == -1) {
			perror("unable to open input file");
			break;
		}
		struct stat stat;
		if (fstat(fd, &stat) == -1) {
			perror("unable to get bitstream file size");
			break;
		}

		uint8_t *file_src = malloc(stat.st_size);
		uint8_t *spi_src = malloc(stat.st_size);
		if (!file_src) {
			perror("unable to alloc memory for buffer");
			break;
		}
		if (read(fd, file_src, stat.st_size) != stat.st_size) {
			perror("unable to read from file");
			free(file_src);
			break;
		}
		close(fd);

		spiRead(spi, 0, spi_src, stat.st_size);

		int offset;
		for (offset = 0; offset < stat.st_size; offset++) {
			if (file_src[offset] != spi_src[offset])
				printf("%9d: file: %02x   spi: %02x\n",
offset, file_src[offset], spi_src[offset]);
		}
		break;
	}

	case OP_SPI_PEEK: {
		fpgaReset(fpga);
		spiSetType(spi, ST_QPI);
		uint8_t page[256];
		spiRead(spi, peek_offset, page, sizeof(page));
		print_hex_offset(stdout, page, sizeof(page), 0, 0);
		break;
	}

	case OP_FPGA_BOOT: {
		spiHold(spi);
		spiSwapTxRx(spi);
		fpgaResetSlave(fpga);

		fprintf(stderr, "FPGA Done? %d\n", fpgaDone(fpga));

		int fd = open(op_filename, O_RDONLY);
		if (fd == -1) {
			perror("unable to open fpga bitstream");
			break;
		}

		spiBegin(spi);

		uint8_t bfr[32768];
		int count;
		while ((count = read(fd, bfr, sizeof(bfr))) > 0) {
			int i;
			for (i = 0; i < count; i++)
				spiTx(spi, bfr[i]);
		}
		if (count < 0) {
			perror("unable to read from fpga bitstream file");
			break;
		}
		close(fd);
		for (count = 0; count < 500; count++)
			spiTx(spi, 0xff);
		fprintf(stderr, "FPGA Done? %d\n", fpgaDone(fpga));
		spiEnd(spi);

		spiSwapTxRx(spi);
		spiUnhold(spi);
		break;
	}

	case OP_FPGA_RESET:
		printf("resetting fpga\n");
		fpgaResetMaster(fpga);
		break;

	default:
		fprintf(stderr, "error: unknown operation\n");
		break;
	}

	fpgaFree(&fpga);
	spiFree(&spi);

	return 0;
}
