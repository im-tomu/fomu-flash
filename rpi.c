#include <bcm2835.h>
#include <stdio.h>
#include <stdint.h>
// #include <unistd.h>

void gpioSetMode(unsigned gpio, unsigned mode) {
   // printf("Set mode %i to %i", gpio, mode);
   // getchar( );
   //usleep(10);
   bcm2835_gpio_fsel(gpio, mode);
}

int gpioRead(unsigned gpio) {
   //bcm2835_gpio_fsel(gpio, 0);
   //usleep(10);
   int res = bcm2835_gpio_lev(gpio);
   //  printf("Read pin gpio %i, at level %i", gpio, res);
   //  getchar( );
   return bcm2835_gpio_lev(res);
}

void gpioWrite(unsigned gpio, unsigned level) {
   //bcm2835_gpio_fsel(gpio, 1);
   //usleep(10);
   bcm2835_gpio_write(gpio, level);
   //  printf("Write pin gpio");
   //  printf("Write pin gpio %i, at level %i", gpio, level);
    // getchar( );
}

int gpioInitialise(void) {
// #define S_MOSI RPI_BPLUS_GPIO_J8_33  // GPIO 13
// #define S_MISO RPI_BPLUS_GPIO_J8_31  // GPIO 6
// #define S_CLK RPI_BPLUS_GPIO_J8_36   // GPIO 16
// #define S_CE0 RPI_BPLUS_GPIO_J8_32   // GPIO 12
// #define S_HOLD RPI_BPLUS_GPIO_J8_22  // GPIO 25
// #define S_WP RPI_BPLUS_GPIO_J8_18    // GPIO 24
// static unsigned int F_RESET = RPI_BPLUS_GPIO_J8_37;  // GPIO 26
// #define F_DONE RPI_BPLUS_GPIO_J8_29  // GPIO 5
   
   // bcm2835_set_debug(1);
   if (!bcm2835_init())
      return 1;
   // printf("MOSI is %i", RPI_BPLUS_GPIO_J8_33);
   // printf("MISO is %i", RPI_BPLUS_GPIO_J8_31);
   // printf("CLK is %i", RPI_BPLUS_GPIO_J8_36);
   // printf("CS is %i", RPI_BPLUS_GPIO_J8_32);
   // printf("HOLD is %i", RPI_BPLUS_GPIO_J8_22);
   // printf("WP is %i", RPI_BPLUS_GPIO_J8_18);
   // printf("RESET is %i", RPI_BPLUS_GPIO_J8_37);
   // printf("DONE is %i", RPI_BPLUS_GPIO_J8_29);
   return 0;
}
