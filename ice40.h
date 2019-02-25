#ifndef _ICE40_H
#define _ICE40_H

#include <stdio.h>
#include <stdint.h>

typedef struct irw_file
{
    FILE *f;
    uint16_t crc;
    uint32_t offset;
    void *hook_data;
    int (*read_hook)(void *data);
    int (*write_hook)(void *data, uint8_t b);
} IRW_FILE;

struct irw_file *irw_open(const char *filename, const char *mode);
struct irw_file *irw_open_fake(void *hook_data,
                               int (*read_hook)(void *data),
                               int (*write_hook)(void *data, uint8_t b));
int irw_readb(struct irw_file *f);
int irw_writeb(struct irw_file *f, int c);
void irw_close(struct irw_file **f);

int ice40_patch(struct irw_file *f, struct irw_file *rom,
                struct irw_file *o, uint32_t byte_count);

#endif /* _ICE40_H */