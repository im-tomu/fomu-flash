#include <assert.h>
#include <string.h>
#include "ice40.h"

#define MAX(x, y) (x) > (y) ? (x) : (y)
#define DEBUG_PRINT(...)

// The number of words forward and backward to look for matches.
// Should be at least 16.
#define DIFF_FUZZ 32

struct Ice40Bitstream
{
    uint32_t offset;
    uint32_t current_bank;
    uint32_t current_width;
    uint32_t current_height;
    uint32_t current_offset;

    uint32_t cram_width;
    uint32_t cram_height;

    uint32_t bram_width;
    uint32_t bram_height;

    uint16_t crc_value;

    uint8_t warmboot;
    uint8_t nosleep;

    uint8_t frequency_range;
};

static void update_crc16(uint16_t *crc, uint8_t byte)
{
    // CRC-16-CCITT, Initialize to 0xFFFF, No zero padding
    for (int i = 7; i >= 0; i--)
    {
        uint16_t xor_value = ((*crc >> 15) ^ ((byte >> i) & 1)) ? 0x1021 : 0;
        *crc = (*crc << 1) ^ xor_value;
    }
}

static uint32_t get_bit_offset(int x, int total_bits) {
    // return (8192 * (x & 7)) + (x >> 3);
    int bitshift = ffs(total_bits)-1;
    return ((x * 8192) % total_bits) + ((x*8192) >> bitshift);
}

uint32_t xorshift32(uint32_t x)
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	x = x ^ (x << 13);
	x = x ^ (x >> 17);
	x = x ^ (x << 5);
	return x;
}

uint32_t get_rand(uint32_t x) {
    uint32_t out = 0;
    int i;
    for (i = 0; i < 32; i++) {
        x = xorshift32(x);
        if ((x & 1) == 1)
            out = out | (1 << i);
    }
    return out;
}

static uint32_t fill_rand(uint32_t *bfr, int count) {
    int i;
    uint32_t last = 1;
    for (i = 0; i < count / 4; i++) {
        last = get_rand(last);
        bfr[i] = last;
    }
    return i;
}

uint32_t swap_u32(uint32_t word) {
    return (((word >> 24) & 0x000000ff)
             | ((word >> 8) & 0x0000ff00)
             | ((word << 8) & 0x00ff0000)
             | ((word << 24) & 0xff000000));
}

struct irw_file *irw_open(const char *filename, const char *mode)
{
    struct irw_file *f = malloc(sizeof(*f));
    memset(f, 0, sizeof(*f));
    f->f = fopen(filename, mode);
    return f;
}

struct irw_file *irw_open_fake(void *hook_data,
                               int (*read_hook)(void *data),
                               int (*write_hook)(void *data, uint8_t b)) {
   struct irw_file *f = malloc(sizeof(*f));
    memset(f, 0, sizeof(*f));
    f->read_hook = read_hook;
    f->write_hook = write_hook;
    f->hook_data = hook_data;
    return f;
}

int irw_readb(struct irw_file *f)
{
    int val;
    if (f->read_hook)
        val = f->read_hook(f->hook_data);
    else
        val = fgetc(f->f);
    if (val == EOF)
        return EOF;
    update_crc16(&f->crc, val);
    return val;
}

int irw_writeb(struct irw_file *f, int c) {
    update_crc16(&f->crc, c);

    if (f->write_hook)
        return f->write_hook(f->hook_data, c);
    else
        return fputc(c, f->f);
}

void irw_close(struct irw_file **f) {
    if (!f)
        return;
    if (!*f)
        return;
    if (!(*f)->f)
        return;
    fclose((*f)->f);
    *f = NULL;
}

uint8_t get_bit(uint32_t *field, uint32_t offset)
{
    // printf("offset&31: %d\n", offset & 31);
    // printf("offset/sizeof(*field): %d\n", offset >> 5);
    assert(offset < 65536);
    return !!(field[offset >> 5] & (1 << (offset & 31)));
}

void set_bit(uint32_t *field, uint32_t offset)
{
    assert(offset < 65536);
    field[offset >> 5] |= (1 << (offset & 31));
}

void clear_bit(uint32_t *field, uint32_t offset)
{
    assert(offset < 65536);
    field[offset >> 5] &= ~(1 << (offset & 31));
}

int ice40_patch(struct irw_file *f, struct irw_file *rom,
                struct irw_file *o, uint32_t byte_count)
{
    uint32_t preamble = 0;
    uint8_t wakeup = 0;
    struct Ice40Bitstream bs;
    uint32_t input_rom[byte_count / sizeof(uint32_t)];
    uint32_t input_rand[byte_count / sizeof(uint32_t)];
    uint32_t output_rand[byte_count / sizeof(uint32_t)];
    uint32_t output_rom[byte_count / sizeof(uint32_t)];
    uint8_t *i8 = (uint8_t *)input_rom;
    uint16_t *ora16 = (uint16_t *)output_rand;
    uint16_t *oro16 = (uint16_t *)output_rom;
    unsigned int ora_ptr = 0;
    unsigned int ora_matches = 0;
    unsigned int input_ptr;
    int b;
    int errors = 0;

    memset(&bs, 0, sizeof(bs));

    // Read the ROM into a source buffer
    memset(input_rom, 0, sizeof(input_rom));
    input_ptr = 0;
    while ((b = irw_readb(rom)) != EOF)
        i8[input_ptr++] = b;
    DEBUG_PRINT("read %d bytes from rom\n", input_ptr);

    // Generate our reference pattern
    memset(input_rand, 0, sizeof(input_rand));
    fill_rand(input_rand, sizeof(input_rand));

    // Swap either the input or output data, as necessary
    for (input_ptr = 0; input_ptr < sizeof(input_rom)/4; input_ptr++) {
        // input_rand[input_ptr] = swap_u32(input_rand[input_ptr]);
        // input_rom[input_ptr] = swap_u32(input_rom[input_ptr]);
    }

    // Spray the reference pattern and ROM like they would exist in the FPGA
    for (input_ptr = 0; input_ptr < sizeof(input_rom) * 8; input_ptr++) {
        int bit;
        bit = get_bit(input_rand, get_bit_offset(input_ptr, sizeof(input_rand)*8));
        if (bit)
            set_bit(output_rand, input_ptr);
        else
            clear_bit(output_rand, input_ptr);

        bit = get_bit(input_rom, get_bit_offset(input_ptr, sizeof(input_rom)*8));
        if (bit)
            set_bit(output_rom, input_ptr);
        else
            clear_bit(output_rom, input_ptr);
    }

    while (1)
    {
        b = irw_readb(f);
        if (b == EOF)
            break;
        irw_writeb(o, b);

        preamble = (preamble << 8) | b;
        if (preamble == 0x7eaa997e)
        {
            // DEBUG_PRINT("found preamble at %d\n", bs.offset);
            break;
        }
    }

    while (!wakeup)
    {
        int b = irw_readb(f);
        if (b == EOF)
        {
            // DEBUG_PRINT("reached end of file\n");
            break;
        }
        irw_writeb(o, b);

        uint8_t cmd = b >> 4;
        uint8_t payload_len = b & 0xf;
        uint32_t payload = 0;
        uint8_t last0, last1;
        unsigned int i;
        for (i = 0; i < payload_len; i++)
        {
            b = irw_readb(f);
            payload = (payload << 8) | (b & 0xff);

            // Don't write the CRC16 out, since we'll do that later on.
            if (cmd != 2)
                irw_writeb(o, b);
        }

        switch (cmd)
        {
        case 0:
            switch (payload)
            {
            case 1:
                DEBUG_PRINT("CRAM data (bank %d): %d x %d @ 0x%08x; %d bits = %d bytes\n",
                       bs.current_bank,
                       bs.current_width,
                       bs.current_height,
                       bs.current_offset,
                       bs.current_width * bs.current_height,
                       (bs.current_width * bs.current_height) / 8);
                bs.cram_width = MAX(bs.cram_width, bs.current_width);
                bs.cram_height = MAX(bs.cram_height, bs.current_height);
                for (i = 0; i < ((bs.current_width * bs.current_height) / 8); i++)
                {
                    irw_writeb(o, irw_readb(f));
                }
                last0 = irw_readb(f);
                last1 = irw_readb(f);
                if (last0 || last1)
                {
                    printf("expected 0x0000 after CRAM data, got %02x %02x\n", last0, last1);
                }
                irw_writeb(o, last0);
                irw_writeb(o, last1);
                break;
            case 3:
                DEBUG_PRINT("BRAM data (bank %d): %d x %d @ 0x%08x; %d bits = %d bytes\n",
                       bs.current_bank,
                       bs.current_width,
                       bs.current_height,
                       bs.current_offset,
                       bs.current_width * bs.current_height,
                       (bs.current_width * bs.current_height) / 8);
                bs.bram_width = MAX(bs.bram_width, bs.current_width);
                bs.bram_height = MAX(bs.bram_height, bs.current_height);
                for (i = 0; i < ((bs.current_width * bs.current_height) / 8); i += 2)
                {
                    uint16_t word = 
                        ((irw_readb(f) << 8) & 0x0000ff00)
                        |
                        ((irw_readb(f) << 0) & 0x000000ff)
                        ;
                    int i;

                    if (word) {
                        int found = 0;
                        int start = ora_ptr - DIFF_FUZZ;
                        int end = ora_ptr + DIFF_FUZZ;
                        if (start < 0)
                            start = 0;
                        if (end > (byte_count - 1))
                            end = (byte_count - 1);
                        for (i = start; i < end; i++) {
                            if (ora16[i] == word) {
                                found = 1;
                                word = oro16[i];
                                ora_matches++;
                                ora_ptr = i;
                                break;
                            }
                        }
                        if (!found) {
                            printf("couldn't find word %04x (ora_ptr: %d  matches: %d)\n", word, ora_ptr, ora_matches);
                            errors++;
                        }
                    }
                    else {
                        ora_ptr++;
                        // DEBUG_PRINT("word is 0, ptr is %d\n", ora_ptr);
                    }
                    irw_writeb(o, word >> 8);
                    irw_writeb(o, word);
                }
                last0 = irw_readb(f);
                last1 = irw_readb(f);
                if (last0 || last1)
                {
                    printf("expected 0x0000 after BRAM data, got %02x %02x\n", last0, last1);
                }
                irw_writeb(o, last0);
                irw_writeb(o, last1);
                break;

            // Reset CRC
            case 5:
                f->crc = 0xffff;
                o->crc = 0xffff;
                break;

            // Wakeup
            case 6:
                wakeup = 1;
                break;

            default:
                printf("unrecognized command 0x%02x 0x%02x\n", cmd, payload);
                break;
            }
            break;

        // Set current bank
        case 1:
            bs.current_bank = payload;
            ora_ptr = 0;
            ora_matches = 0;
            // printf("setting bank number to %d\n", bs.current_bank);
            break;

        // Validate CRC16
        case 2:
            DEBUG_PRINT("crc check (%04x == %04x)\n", f->crc, 0);
            uint16_t crc16 = o->crc;
            irw_writeb(o, crc16 >> 8);
            irw_writeb(o, crc16);
            break;

        // Set frequency range
        case 5:
            switch (payload)
            {
            case 0:
                bs.frequency_range = 0;
                break;
            case 1:
                bs.frequency_range = 1;
                break;
            case 2:
                bs.frequency_range = 2;
                break;
            default:
                printf("unknown frequency range payload: %02x\n", payload);
                break;
            }
            break;

        // Set current width
        case 6:
            bs.current_width = payload + 1;
            break;

        // Set current height
        case 7:
            bs.current_height = payload;
            break;

        // Set current ofset
        case 8:
            bs.current_offset = payload;
            break;

        // Set flags
        case 9:
            switch (payload)
            {
            case 0:
                bs.warmboot = 0;
                bs.nosleep = 0;
                break;
            case 1:
                bs.warmboot = 0;
                bs.nosleep = 1;
                break;
            case 32:
                bs.warmboot = 1;
                bs.nosleep = 0;
                break;
            case 33:
                bs.warmboot = 1;
                bs.nosleep = 1;
                break;
            default:
                printf("unrecognized feature flags: %02x\n", payload);
                break;
            }
            break;

        default:
            printf("unrecognized command: %02x\n", cmd);
            break;
        }
    }

    // Padding
    irw_writeb(o, 0);

    return errors;
}
