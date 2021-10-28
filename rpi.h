#ifndef RPI_H_
#define RPI_H_

#include <bcm2835.h>
/* gpio modes. */
#define PI_INPUT  BCM2835_GPIO_FSEL_INPT
#define PI_OUTPUT BCM2835_GPIO_FSEL_OUTP
#define PI_ALT0   BCM2835_GPIO_FSEL_ALT0 
#define PI_ALT1   BCM2835_GPIO_FSEL_ALT1
#define PI_ALT2   BCM2835_GPIO_FSEL_ALT2 
#define PI_ALT3   BCM2835_GPIO_FSEL_ALT3 
#define PI_ALT4   BCM2835_GPIO_FSEL_ALT4
#define PI_ALT5   BCM2835_GPIO_FSEL_ALT5 

void gpioSetMode(unsigned gpio, unsigned mode);
int gpioGetMode(unsigned gpio);
int gpioRead(unsigned gpio);
void gpioWrite(unsigned gpio, unsigned level);
int gpioInitialise(void);

#endif /* RPI_H_ */
