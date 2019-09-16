#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static volatile uint32_t piModel = 1;

static volatile uint32_t piPeriphBase = 0x20000000;
static volatile uint32_t piBusAddr = 0x40000000;

#define SYST_BASE  (piPeriphBase + 0x003000)
#define DMA_BASE   (piPeriphBase + 0x007000)
#define CLK_BASE   (piPeriphBase + 0x101000)
#define GPIO_BASE  (piPeriphBase + 0x200000)
#define UART0_BASE (piPeriphBase + 0x201000)
#define PCM_BASE   (piPeriphBase + 0x203000)
#define SPI0_BASE  (piPeriphBase + 0x204000)
#define I2C0_BASE  (piPeriphBase + 0x205000)
#define PWM_BASE   (piPeriphBase + 0x20C000)
#define BSCS_BASE  (piPeriphBase + 0x214000)
#define UART1_BASE (piPeriphBase + 0x215000)
#define I2C1_BASE  (piPeriphBase + 0x804000)
#define I2C2_BASE  (piPeriphBase + 0x805000)
#define DMA15_BASE (piPeriphBase + 0xE05000)

#define DMA_LEN   0x1000 /* allow access to all channels */
#define CLK_LEN   0xA8
#define GPIO_LEN  0xB4
#define SYST_LEN  0x1C
#define PCM_LEN   0x24
#define PWM_LEN   0x28
#define I2C_LEN   0x1C

#define GPSET0 7
#define GPSET1 8

#define GPCLR0 10
#define GPCLR1 11

#define GPLEV0 13
#define GPLEV1 14

#define GPPUD     37
#define GPPUDCLK0 38
#define GPPUDCLK1 39

#define SYST_CS  0
#define SYST_CLO 1
#define SYST_CHI 2

#define CLK_PASSWD  (0x5A<<24)

#define CLK_CTL_MASH(x)((x)<<9)
#define CLK_CTL_BUSY    (1 <<7)
#define CLK_CTL_KILL    (1 <<5)
#define CLK_CTL_ENAB    (1 <<4)
#define CLK_CTL_SRC(x) ((x)<<0)

#define CLK_SRCS 4

#define CLK_CTL_SRC_OSC  1  /* 19.2 MHz */
#define CLK_CTL_SRC_PLLC 5  /* 1000 MHz */
#define CLK_CTL_SRC_PLLD 6  /*  500 MHz */
#define CLK_CTL_SRC_HDMI 7  /*  216 MHz */

#define CLK_DIV_DIVI(x) ((x)<<12)
#define CLK_DIV_DIVF(x) ((x)<< 0)

#define CLK_GP0_CTL 28
#define CLK_GP0_DIV 29
#define CLK_GP1_CTL 30
#define CLK_GP1_DIV 31
#define CLK_GP2_CTL 32
#define CLK_GP2_DIV 33

#define CLK_PCM_CTL 38
#define CLK_PCM_DIV 39

#define CLK_PWM_CTL 40
#define CLK_PWM_DIV 41


static volatile uint32_t  *gpioReg = MAP_FAILED;
static volatile uint32_t  *systReg = MAP_FAILED;
static volatile uint32_t  *clkReg  = MAP_FAILED;

#define PI_BANK (gpio>>5)
#define PI_BIT  (1<<(gpio&0x1F))

/* BCM2711 (RPi 4) pullups are different. */
#define GPPUPPDN0 57 /* pins 15:0  */
#define GPPUPPDN1 58 /* pins 31:16 */
#define GPPUPPDN2 59 /* pins 47:32 */
#define GPPUPPDN3 60 /* pins 57:48 */

/* gpio modes. */

#define PI_INPUT  0
#define PI_OUTPUT 1
#define PI_ALT0   4
#define PI_ALT1   5
#define PI_ALT2   6
#define PI_ALT3   7
#define PI_ALT4   3
#define PI_ALT5   2

void gpioSetMode(unsigned gpio, unsigned mode) {
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

int gpioGetMode(unsigned gpio) {
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   return (*(gpioReg + reg) >> shift) & 7;
}

/* Values for pull-ups/downs off, pull-down and pull-up. */

#define PI_PUD_OFF  0
#define PI_PUD_DOWN 1
#define PI_PUD_UP   2

void gpioSetPullUpDown(unsigned gpio, unsigned pud) {
  if (piModel != 4) {
   *(gpioReg + GPPUD) = pud;

   usleep(20);

   *(gpioReg + GPPUDCLK0 + PI_BANK) = PI_BIT;

   usleep(20);
  
   *(gpioReg + GPPUD) = 0;

   *(gpioReg + GPPUDCLK0 + PI_BANK) = 0;
  } else {

    int pullreg = GPPUPPDN0 + (gpio>>4); /* Which pull-up control reg to use */
    int pullshift = (gpio & 0xf) << 1;   /* Bit position within reg */
    unsigned int pullbits;
    unsigned int pull;

    switch (pud)
      {
      case PI_PUD_OFF: pull = 0; break;
      case PI_PUD_UP: pull = 1; break;
      case PI_PUD_DOWN: pull = 2; break;
      default:
       return;
      }

    pullbits = *(gpioReg + pullreg);
    pullbits &= ~(3 << pullshift);
    pullbits |= (pull << pullshift);
    *(gpioReg + pullreg) = pullbits;

  }
}

int gpioRead(unsigned gpio) {
   if ((*(gpioReg + GPLEV0 + PI_BANK) & PI_BIT) != 0) return 1;
   else                                         return 0;
}

void gpioWrite(unsigned gpio, unsigned level) {
   if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

void gpioTrigger(unsigned gpio, unsigned pulseLen, unsigned level) {
   if (level == 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;

   usleep(pulseLen);

   if (level != 0) *(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
   else            *(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

/* Bit (1<<x) will be set if gpio x is high. */

uint32_t gpioReadBank1(void) { return (*(gpioReg + GPLEV0)); }
uint32_t gpioReadBank2(void) { return (*(gpioReg + GPLEV1)); }

/* To clear gpio x bit or in (1<<x). */

void gpioClearBank1(uint32_t bits) { *(gpioReg + GPCLR0) = bits; }
void gpioClearBank2(uint32_t bits) { *(gpioReg + GPCLR1) = bits; }

/* To set gpio x bit or in (1<<x). */

void gpioSetBank1(uint32_t bits) { *(gpioReg + GPSET0) = bits; }
void gpioSetBank2(uint32_t bits) { *(gpioReg + GPSET1) = bits; }

static void piAssignAddresses(uint32_t rev) {
   // Note: early models were serialized, and have `rev` values
   // ranging from 0 to 15.  Happily, these all will map to
   // the BCM2835 processor.
   switch ((rev >> 12) & 0xf) {
   case 0: // BCM2835
      piPeriphBase = 0x20000000;
      piBusAddr = 0x40000000;
   break;

   case 1: // BCM2836
   case 2: // BCM2837
      piPeriphBase = 0x3F000000;
      piBusAddr = 0xC0000000;
   break;

   case 3: // BCM2711 (Pi 4)
      piPeriphBase = 0xFE000000;
      piBusAddr = 0x40000000;
   break;

   default:
      fprintf(stderr, "Unrecognized Raspberry Pi revision: 0x%08x (processor rev %d)\n",
            rev, (rev >> 12) & 0xf);
   }
   return;
}

unsigned gpioHardwareRevision(void) {
   static uint32_t rev = 0;

   int found_rev = 0;
   FILE * filp;
   char buf[512];

   if (rev) return rev;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         // Pull out the "revision" and "Model", from
         // https://www.raspberrypi.org/documentation/hardware/raspberrypi/revision-codes/README.md
         if (!strncasecmp("revision", buf, 8))
         {
            // Revision        : a22082
            found_rev = 1;
            char *separator = strchr(buf, ':');
            if (separator && separator[0] && separator[1] && separator[2]) {
               rev = strtol(&separator[2], NULL, 16);
            }
         }
      }

      piAssignAddresses(rev);

      fclose(filp);
   }

   if (!found_rev) {
      fprintf(stderr, "Warning: couldn't find Raspberry Pi revision in /proc/cpuinfo\n");
   }

   return rev;
}

/* Returns the number of microseconds after system boot. Wraps around
   after 1 hour 11 minutes 35 seconds.
*/
uint32_t gpioTick(void) {
    return systReg[SYST_CLO];
}


/* Map in registers. */
static uint32_t *initMapMem(int fd, uint32_t addr, uint32_t len) {
    return (uint32_t *) mmap(0, len,
       PROT_READ|PROT_WRITE|PROT_EXEC,
       MAP_SHARED|MAP_LOCKED,
       fd, addr);
}

int gpioInitialise(void) {
   int fd;

   gpioHardwareRevision(); /* sets piModel, needed for peripherals address */

   fd = open("/dev/mem", O_RDWR | O_SYNC) ;

   if (fd < 0)
   {
      fprintf(stderr,
         "This program needs root privileges.  Try using sudo\n");
      return -1;
   }

   gpioReg  = initMapMem(fd, GPIO_BASE, GPIO_LEN);
   systReg  = initMapMem(fd, SYST_BASE, SYST_LEN);
   clkReg   = initMapMem(fd, CLK_BASE,  CLK_LEN);

   close(fd);

   if ((gpioReg == MAP_FAILED) ||
       (systReg == MAP_FAILED) ||
       (clkReg == MAP_FAILED))
   {
      fprintf(stderr,
         "Bad, mmap failed\n");
      return -1;
   }
   return 0;
}
