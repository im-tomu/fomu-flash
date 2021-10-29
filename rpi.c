#include <bcm2835.h>
#include <stdio.h>
#include <stdint.h>

void gpioSetMode(unsigned gpio, unsigned mode) {
   bcm2835_gpio_fsel(gpio, mode);
}

int gpioRead(unsigned gpio) {
   return bcm2835_gpio_lev(gpio);
}

void gpioWrite(unsigned gpio, unsigned level) {
   bcm2835_gpio_write(gpio, level);
}

int gpioInitialise(void) {
   if (!bcm2835_init())
      return 1;
   return 0;
}