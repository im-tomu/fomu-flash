# Fomu FPGA Tools

The EVT version of Fomu is a "stretch" PCB with a Raspberry Pi header.  Additionally, the factory test jig for production versions of Fomu has pins that match up with a test jig with the same pinout.

These tools can be used to control an FPGA and its accompanying SPI flash chip.

## Building

To build this repository, simply run `make`.

## Test Jig Setup

The EVT boards can be attached directly to the Raspberry Pi as a "hat".  When building a test jig, attach wires according to the following image:

![Raspberry Pi Pinout](pinout.png)

The only pins that are required are 5V, GND, CRESET, SPI_MOSI, SPI_MISO, SPI_CLK, and SPI_CS.

The Pi's hardware SPI interface must be enabled in the kernel- use
`raspi-config` or add `dtparam=spi=on` to `/boot/config.txt` and reboot before
using.  You can improve performance by attaching SPI_IO2 and SPI_IO3 and running
`fomu-flash` in quad/qpi mode by specifying `-t 4` or `-t q`.

You can get serial interaction by connecting the UART pins, but they are not necessary for flashing.

## Loading a Bitstream

The most basic usecase is to load a program into configuration RAM.  This is a very quick process, and can be used for rapid prototyping.

To load `top.bin`, use the `-f` argument:

```sh
# ./fomu-flash -f top.bin
```

This will reset the FPGA, reset the SPI flash, load the bitstream into the FPGA, and then start running the program.

## Programming SPI Flash

To write a binary file to SPI flash, use `-w`:

```sh
# ./fomu-flash -w top.bin   # Write top.bin to SPI Flash
# ./fomu-flash -r           # Reset the FPGA
```

This will erase just enough of the SPI to hold the new binary file, then flash the binary to SPI.

It will not reset the FPGA.  To do that, you must re-run with `-r`.

## Verifying SPI flash

You can verify the SPI flash was programmed with the `-v` command:

```sh
# ./tomu-flash -v top.bin
```

## Checking SPI Flash was Written

You can "peek" at 256 bytes of SPI with `-p [offset]`.  This can be used to quickly verify that something was written.

## Patching ROM

`fomu-flash` supports patching ROM.  To do this, you must synthesize your bitstream with a fixed random ROM contents.  This is so `fomu-flash` has something to look for.

The Python code for this would look like:

```python
def xorshift32(x):
    x = x ^ (x << 13) & 0xffffffff
    x = x ^ (x >> 17) & 0xffffffff
    x = x ^ (x << 5)  & 0xffffffff
    return x & 0xffffffff

def get_rand(x):
    out = 0
    for i in range(32):
        x = xorshift32(x)
        if (x & 1) == 1:
            out = out | (1 << i)
    return out & 0xffffffff

def get_bit(x):
    return (256 * (x & 7)) + (x >> 3)
```

And the corresponding C code looks like:

```c
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
```

Currently, `fomu-flash` only supports 8192-byte ROMs, though there is no reason why it can't be extended to other sizes.

Specify a ROM to load on the command line with `-l`.