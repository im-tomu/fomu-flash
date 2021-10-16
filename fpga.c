#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include "rpi.h"
#include "fpga.h"

struct ff_fpga {
    struct {
        int reset;
        int done;
        int cs;
    } pins;
};

int fpgaDone(struct ff_fpga *fpga) {
        return gpioRead(fpga->pins.done);
}

int fpgaResetSlave(struct ff_fpga *fpga) {
    // Put the FPGA into reset
    gpioSetMode(fpga->pins.reset, PI_OUTPUT);
    gpioWrite(fpga->pins.reset, 0);

    // Set the CS pin to a GPIO, which will let us control it
    gpioSetMode(fpga->pins.cs, PI_OUTPUT);

    // Set CS to 0, which will put the FPGA into slave mode
    gpioWrite(fpga->pins.cs, 0);

    usleep(10000); // XXX figure out correct sleep length here

    // Bring the FPGA out of reset
    gpioWrite(fpga->pins.reset, 1);

    usleep(1200); // 13.2.SPI Slave Configuration Process

    // Release the CS pin
    // 2019/07/13: Don't release CS pin so as to prevent the SPI
    // flash from waking up.
    //gpioWrite(fpga->pins.cs, 1);

    return 0;
}

int fpgaResetMaster(struct ff_fpga *fpga) {

    // Put the FPGA into reset
    gpioSetMode(fpga->pins.reset, PI_OUTPUT);
    gpioWrite(fpga->pins.reset, 0);

    // Set the CS pin to a GPIO, which will let us control it
    gpioSetMode(fpga->pins.cs, PI_OUTPUT);

    // Set CS to 1, which will put the FPGA into "self boot" mode
    gpioWrite(fpga->pins.cs, 1);

    usleep(10000); // XXX figure out correct sleep length here

    // Bring the FPGA out of reset
    gpioWrite(fpga->pins.reset, 1);

    usleep(1200); // 13.2.SPI Slave Configuration Process

    return 0;
}

int fpgaReset(struct ff_fpga *fpga) {
    // Put the FPGA into reset
    gpioSetMode(fpga->pins.reset, PI_OUTPUT);
    gpioWrite(fpga->pins.reset, 0);
    return 0;
}

int fpgaInit(struct ff_fpga *fpga) {
    // Put the FPGA into reset
    gpioSetMode(fpga->pins.reset, PI_OUTPUT);
    gpioWrite(fpga->pins.reset, 0);

    // Also monitor the C_DONE pin
    gpioSetMode(fpga->pins.done, PI_INPUT);

    return 0;
}

struct ff_fpga *fpgaAlloc(void) {
    struct ff_fpga *fpga = (struct ff_fpga *)malloc(sizeof(struct ff_fpga));
    memset(fpga, 0, sizeof(*fpga));
    return fpga;
}

void fpgaSetPin(struct ff_fpga *fpga, enum fpga_pin pin, int val) {
    switch (pin) {
    case FP_RESET: fpga->pins.reset = val; break;
    case FP_DONE: fpga->pins.done = val; break;
    case FP_CS: fpga->pins.cs = val; break;
    default: fprintf(stderr, "unrecognized pin: %d\n", pin); break;
    }
}

void fpgaFree(struct ff_fpga **fpga) {
    if (!fpga)
        return;
    if (!*fpga)
        return;

    free(*fpga);
    *fpga = NULL;
}
