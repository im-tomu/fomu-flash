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
#include "ice40.h"

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
static unsigned int F_RESET = 27;
#define F_DONE 17

// #define DEBUG_ICE40_PATCH

#ifndef DEBUG_ICE40_PATCH
static int spi_irw_readb(void *data) {
    return spiRx(data);
}

static int spi_irw_writeb(void *data, uint8_t b) {
    spiTx(data, b);
    return b;
}
#endif

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
    OP_SPI_ID,
    OP_SPI_SECURITY_READ,
    OP_SPI_SECURITY_WRITE,
    OP_FPGA_BOOT,
    OP_FPGA_RESET,
    OP_SET_QE,
    OP_UNKNOWN,
};

static int pinspec_to_pinname(char code) {
    switch (code) {
        case '0': return SP_D0;
        case '1': return SP_D1;
        case '2': return SP_D2;
        case '3': return SP_D3;
        case 'o': return SP_MOSI;
        case 'i': return SP_MISO;
        case 'w': return SP_WP;
        case 'h': return SP_HOLD;
        case 'c': return SP_CLK;
        case 's': return SP_CS;
        case 'r': return FP_RESET;
        case 'd': return FP_DONE;
        default: return -1;
    }
}

static int print_pinspec(FILE *stream) {
    fprintf(stream, "Pinspec:\n");
    fprintf(stream, " Name   Description    Default (BCM pin number)\n");
    fprintf(stream, "   0    SPI D0         %d\n", S_D0);
    fprintf(stream, "   1    SPI D1         %d\n", S_D1);
    fprintf(stream, "   2    SPI D2         %d\n", S_D2);
    fprintf(stream, "   3    SPI D3         %d\n", S_D3);
    fprintf(stream, "   o    SPI MOSI       %d\n", S_MOSI);
    fprintf(stream, "   i    SPI MISO       %d\n", S_MISO);
    fprintf(stream, "   w    SPI WP         %d\n", S_WP);
    fprintf(stream, "   h    SPI HOLD       %d\n", S_HOLD);
    fprintf(stream, "   c    SPI CLK        %d\n", S_CLK);
    fprintf(stream, "   s    SPI CS         %d\n", S_CE0);
    fprintf(stream, "   r    FPGA Reset     %d\n", F_RESET);
    fprintf(stream, "   d    FPGA Done      %d\n", F_DONE);
    fprintf(stream, "For example: -g i:23    or -g d:27\n");
    return 0;
}

static int print_program_modes(FILE *stream) {
    fprintf(stream, "    -h        This help page\n");
    fprintf(stream, "    -r        Reset the FPGA and have it boot from SPI\n");
    fprintf(stream, "    -i        Print out the SPI ID code\n");
    fprintf(stream, "    -q        Quiet operation\n");
    fprintf(stream, "    -p offset Peek at 256 bytes of SPI flash at the specified offset\n");
    fprintf(stream, "    -f bin    Load this bitstream directly into the FPGA\n");
    fprintf(stream, "    -l rom    Replace the ROM in the bitstream with this file\n");
    fprintf(stream, "    -w bin    Write this binary into the SPI flash chip\n");
    fprintf(stream, "    -a addr   Change the address to write/read from\n");
    fprintf(stream, "    -v bin    Verify the SPI flash contains this data\n");
    fprintf(stream, "    -s out    Save the SPI flash contents to this file\n");
    fprintf(stream, "    -k n[:f]  Read security register [n], or update it with the contents of file [f]\n");
    fprintf(stream, "    -4        Sets the QE enable bit\n");
    return 0;
}

static int print_help(FILE *stream, const char *progname) {
    fprintf(stream, "Fomu Raspberry Pi Flash Utilities\n");
    fprintf(stream, "Usage:\n");
    fprintf(stream, "%15s  (-[hri] | [-p offset] | [-f bitstream] | \n", progname);
    fprintf(stream, "%15s            [-w bin] | [-v bin] | [-s out] | [-k n[:f]])\n", "");
    fprintf(stream, "                [-g pinspec] [-t spitype] [-b bytes] [-a addr] [-u]\n");
    fprintf(stream, "\n");
    fprintf(stream, "Program mode (pick one):\n");
    print_program_modes(stream);
    fprintf(stream, "\n");
    fprintf(stream, "Configuration options:\n");
    fprintf(stream, "    -g ps     Set the pin assignment with the given pinspec\n");
#ifndef DEBUG_ICE40_PATCH
    fprintf(stream, "    -t type   Set the number of bits to use for SPI (1, 2, 4, or Q)\n");
    fprintf(stream, "    -u        Unlock the SPI Global Block Protect with a 0x98 command\n");
    fprintf(stream, "    -b bytes  Override the size of the SPI flash, in bytes\n");
#endif
    fprintf(stream, "You can remap various pins with -g.  The format is [name]:[number].\n");
    fprintf(stream, "\n");
    fprintf(stream, "The width of SPI can be set with 't [width]'.  Valid widths are:\n");
    fprintf(stream, "    1 - standard 1-bit spi\n");
    fprintf(stream, "    2 - standard 2-bit spi\n");
    fprintf(stream, "    4 - standard 4-bit spi (with 1-bit commands)\n");
    fprintf(stream, "    q - 4-bit qspi (with 4-bit commands)\n");
    fprintf(stream, "\n");
    print_pinspec(stream);
    return 0;
}

static int print_usage_error(FILE *stream) {
    fprintf(stream, "Error: You must only specify one program mode:\n");
    print_program_modes(stream);
    return 1;
}

int main(int argc, char **argv) {
    int opt;
    int ret = 0;
    int fd;
    char *op_filename = NULL;
    struct ff_spi *spi;
    struct ff_fpga *fpga;
    int peek_offset = 0;
    uint32_t addr = 0;
#ifndef DEBUG_ICE40_PATCH
    int spi_flash_bytes = -1;
    enum spi_type spi_type = ST_SINGLE;
#endif
    uint8_t security_reg;
    uint8_t security_val[256];
    enum op op = OP_UNKNOWN;
    struct irw_file *replacement_rom = NULL;
    int quiet = 0;


// Remove initialize delegated to new library

// #ifndef DEBUG_ICE40_PATCH
//     if (gpioInitialise() < 0) {
//         fprintf(stderr, "Unable to initialize GPIO\n");
//         return 1;
//     }

//     // The original Raspberry Pi boards had a different assignment
//     // of pin 13.  All other boards assign it to BCM 27, but the
//     // original had it as BCM 21.
//     if ((gpioHardwareRevision() == 2) || (gpioHardwareRevision() == 3))
//         F_RESET = 21;
// #endif

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

    // by default do not unlock the chip
    spiSetUnlockCmd(spi, NO_UNLOCK_CMD);

    fpgaSetPin(fpga, FP_RESET, F_RESET);
    fpgaSetPin(fpga, FP_DONE, F_DONE);
    fpgaSetPin(fpga, FP_CS, S_CE0);

    while ((opt = getopt(argc, argv, "hiqp:rf:a:b:w:s:2:3:v:g:t:k:l:4:u")) != -1) {
        switch (opt) {

        case 'a':
            addr = strtoul(optarg, NULL, 0);
            break;

        case 'q':
            quiet = 1;
            break;

        case 'r':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_FPGA_RESET;
            break;

#ifndef DEBUG_ICE40_PATCH
        case 'b':
            spi_flash_bytes = strtoul(optarg, NULL, 0);
            break;
        
        case 'u':
            spiSetUnlockCmd(spi, UNLOCK_CMD);
            break;
        case 't':
            switch (*optarg) {
            case '1':
                spi_type = ST_SINGLE;
                break;
            case '2':
                spi_type = ST_DUAL;
                break;
            case '4':
                spi_type = ST_QUAD;
                break;
            case 'q':
                spi_type = ST_QPI;
                break;
            default:
                fprintf(stderr, "Unrecognized SPI speed '%c'.  Valid types are: 1, 2, 4, or q\n", *optarg);
                return 1;
            }
            break;
#endif

        case 'l':
            replacement_rom = irw_open(optarg, "r");
            if (!replacement_rom) {
                perror("couldn't open replacement rom file");
                return 10;
            }
            break;

        case 'k': {
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            char *security_filename = strchr(optarg, ':');

            security_reg = strtoul(optarg, NULL, 0);
            if (security_filename) {
                security_filename++;
                op = OP_SPI_SECURITY_WRITE;
                int fd;
                fd = open(security_filename, O_RDONLY);
                if (fd == -1) {
                    perror("couldn't open security file");
                    return 1;
                }
                memset(security_val, 0, sizeof(security_val));
                if (-1 == read(fd, security_val, sizeof(security_val))) {
                    perror("couldn't read from security file");
                    return 2;
                }
                close(fd);
            }
            else {
                op = OP_SPI_SECURITY_READ;
            }
            break;
        }

        case 'i':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SPI_ID;
            break;

        case 'p':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SPI_PEEK;
            peek_offset = strtoul(optarg, NULL, 0);
            break;

        case 'g':
            if ((optarg[0] == '\0') || (optarg[1] != ':')) {
                fprintf(stderr, "-g requires a pinspec.  Usage:\n");
                print_pinspec(stderr);
                return 1;
            }

            spiSetPin(spi, pinspec_to_pinname(optarg[0]), strtoul(optarg+2, NULL, 0));
            break;

        case '2':
            spiSetPin(spi, SP_D2, strtoul(optarg, NULL, 0));
            break;

        case '3':
            spiSetPin(spi, SP_D3, strtoul(optarg, NULL, 0));
            break;

        case 'f':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_FPGA_BOOT;
            if (op_filename)
                free(op_filename);
            op_filename = strdup(optarg);
            break;

        case 'w':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SPI_WRITE;
            if (op_filename)
                free(op_filename);
            op_filename = strdup(optarg);
            break;

        case 'v':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SPI_VERIFY;
            if (op_filename)
                free(op_filename);
            op_filename = strdup(optarg);
            break;

        case 's':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SPI_READ;
            if (op_filename)
                free(op_filename);
            op_filename = strdup(optarg);
            break;

	case '4':
            if (op != OP_UNKNOWN)
                return print_usage_error(stdout);
            op = OP_SET_QE;
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

#ifndef DEBUG_ICE40_PATCH
    fpgaInit(fpga);
    fpgaReset(fpga);
    spiInit(spi);

    spiSetType(spi, spi_type);

    if (spi_flash_bytes != -1)
        spiOverrideSize(spi, spi_flash_bytes);
#else
    if (op != OP_FPGA_BOOT) {
        printf("DEBUG_ICE40_PATCH requires you load a bitstream with '-f'\n");
        return 9;
    }
    if (!replacement_rom) {
        printf("DEBUG_ICE40_PATCH requires you load a replacement rom with '-l'\n");
        return 10;
    }
#endif

    switch (op) {
    case OP_SPI_ID: {
        struct spi_id id = spiId(spi);
        printf("Manufacturer ID: %s (%02x)\n", id.manufacturer, id.manufacturer_id);
        if (id.manufacturer_id != id._manufacturer_id)
            printf("!! JEDEC Manufacturer ID: %02x\n",
            id._manufacturer_id);
        printf("Memory model: %s (%02x)\n", id.model, id.memory_type);
        printf("Memory size: %s (%02x)\n", id.capacity, id.memory_size);
        printf("Device ID: %02x\n", id.device_id);
        if (id.device_id != id.signature)
            printf("!! Electronic Signature: %02x\n", id.signature);
        printf("Serial number: %02x %02x %02x %02x\n", id.serial[0], id.serial[1], id.serial[2], id.serial[3]);
        printf("Status 1: %02x\n", spiReadStatus(spi, 1));
        printf("Status 2: %02x\n", spiReadStatus(spi, 2));
        printf("Status 3: %02x\n", spiReadStatus(spi, 3));
        break;
    }

    case OP_SPI_SECURITY_WRITE: {
        printf("Updating security register %d.\n", security_reg);
        spiWriteSecurity(spi, security_reg, security_val);
        break;
    }

    case OP_SPI_SECURITY_READ: {
        uint8_t security[256];
        printf("Security register %d contents:\n", security_reg);
        spiReadSecurity(spi, security_reg, security);
        print_hex(security, sizeof(security), 0);
        break;
    }

    case OP_SPI_READ: {
        struct spi_id id = spiId(spi);
        if (id.bytes == -1) {
            fprintf(stderr, "unknown spi flash size -- specify with -b\n");
            return 1;
        }

        fd = open(op_filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd == -1) {
            perror("unable to open output file");
            break;
        }
        uint8_t *bfr = malloc(id.bytes);
        if (!bfr) {
            perror("unable to allocate memory for spi");
            return 1;
        }
        spiRead(spi, addr, bfr, id.bytes);
        if (write(fd, bfr, id.bytes) != id.bytes) {
            perror("unable to write SPI flash image to disk");
            break;
        }
        close(fd);
        free(bfr);
        break;
    }

    case OP_SPI_WRITE: {
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
        spiWrite(spi, addr, bfr, stat.st_size, quiet);
        break;
    }

    case OP_SPI_VERIFY: {
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

        spiRead(spi, addr, spi_src, stat.st_size);

        unsigned int offset;
        for (offset = addr; offset < stat.st_size + addr; offset++) {
            if (file_src[offset - addr] != spi_src[offset - addr]) {
                ret++;
                if (!quiet)
                    printf("%9d: file: %02x   spi: %02x\n", offset, file_src[offset - addr], spi_src[offset - addr]);
            }
        }
        break;
    }

    case OP_SPI_PEEK: {
        uint8_t page[256];
        spiRead(spi, peek_offset, page, sizeof(page));
        print_hex_offset(stdout, page, sizeof(page), 0, 0);
        break;
    }

    case OP_FPGA_BOOT: {
        int count;
#ifndef DEBUG_ICE40_PATCH
        spiHold(spi);
        spiSwapTxRx(spi);
        fpgaResetSlave(fpga);
        fprintf(stderr, "FPGA Done? %d\n", fpgaDone(fpga));
        spiBegin(spi);
#endif
        if (replacement_rom) {
            IRW_FILE *bitstream = irw_open(op_filename, "r");
            if (!bitstream) {
                perror("unable to open fpga bitstream");
                break;
            }
#ifdef DEBUG_ICE40_PATCH
            IRW_FILE *spidev = irw_open("foboot-patched.bin", "w");
            return ice40_patch(bitstream, replacement_rom, spidev, 8192);
#else
            IRW_FILE *spidev = irw_open_fake(spi, spi_irw_readb, spi_irw_writeb);
#endif
            ice40_patch(bitstream, replacement_rom, spidev, 8192);
        }
        else {
            uint8_t bfr[32768];
            int fd = open(op_filename, O_RDONLY);
            if (fd == -1) {
                perror("unable to open fpga bitstream");
                break;
            }
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
        }
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

    case OP_SET_QE:
        spiSetQe(spi);
	break;

    default:
        fprintf(stderr, "error: unknown operation\n");
        break;
    }

    spiFree(&spi);
    fpgaFree(&fpga);

    return ret;
}
