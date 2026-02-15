
typedef char cc_t;
typedef int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 12

#define VEOF   1  // end of file character
#define VEOL   2  // end of line character
#define VERASE 3  // erase character
#define VINTR  4  // interrupt character
#define VKILL  5  // kill line character
#define VMIN   6  // minimum number of characters for noncanonical read
#define VQUIT  7  // quit character
#define VSTART 8  // enable output character
#define VSTOP  9  // stop output character
#define VSUSP  10 // suspend character (send SIGSTOP)
#define VTIME  11 // timeout for noncanonical read (in deciseconds)


typedef struct termios {
    tcflag_t c_iflag;   // input modes
    tcflag_t c_oflag;   // output modes
    tcflag_t c_cflag;   // control modes
    tcflag_t c_lflag;   // local modes
    cc_t c_cc[NCCS];    // control characters
} termios_t;

#define CBAUD  15
#define CSIZE  (3UL << 4)
#define CS5    (0UL << 4)
#define CS6    (1UL << 4)
#define CS7    (2UL << 4)
#define CS8    (3UL << 4)
#define CSTOPB (1UL << 6)
#define CREAD  (1UL << 7)
#define PARENB (1UL << 8)
#define PARODD (1UL << 9)
#define HUPCL  (1UL << 10)
#define CLOCAL (1UL << 11)
#define IBSHIFT 12
#define CIBAUD (CBAUD << IBSHIFT)

#define IGNBRK  (1UL << 0)
#define BRKINT  (1UL << 1)
#define IGNPAR  (1UL << 2)
#define PARMRK  (1UL << 3)
#define INPCK   (1UL << 4)
#define ISTRIP  (1UL << 5)
#define INLCR   (1UL << 6)
#define IGNCR   (1UL << 7)
#define ICRNL   (1UL << 8)
#define IUCLC   (1UL << 9)
#define IXON    (1UL << 10)
#define IXANY   (1UL << 11)
#define IXOFF   (1UL << 12)
#define IMAXBEL (1UL << 13)

#define OPOST   (1UL << 0)
#define OLCUC   (1UL << 1)
#define ONLCR   (1UL << 2)
#define OCRNL   (1UL << 3)
#define ONOCR   (1UL << 4)
#define ONLRET  (1UL << 5)
#define OFILL   (1UL << 6)
#define OFDEL   (1UL << 7)

#define ISIG    (1UL << 0)
#define ICANON  (1UL << 1)
#define ECHO    (1UL << 2)
#define ECHOE   (1UL << 3)
#define ECHOK   (1UL << 4)
#define ECHONL  (1UL << 5)
#define ECHOCTL (1UL << 6)
#define ECHOKE  (1UL << 7)
#define NOFLSH  (1UL << 8)
#define TOSTOP  (1UL << 9)
#define IEXTEN  (1UL << 10)

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

// baudrate in the big 26 </3
#define B0        0
#define B50       1
#define B75       2
#define B110      3
#define B134      4
#define B150      5
#define B200      6
#define B300      7
#define B600      8
#define B1200     9
#define B1800     10
#define B2400     11
#define B4800     12
#define B9600     13
#define B19200    14
#define B38400    15
#define B57600    16
#define B115200   17
#define B230400   18
#define B460800   19
#define B500000   20
#define B576000   21
#define B921600   22
#define B1000000  23
#define B1152000  24
#define B1500000  25
#define B2000000  26

#define TCIOFLUSH 0
#define TCIFLUSH  1
#define TCOFLUSH  2

#define VT_ACTIVATE   0x5606
#define VT_WAITACTIVE 0x5607
#define VT_GETSTATE   0x5603
#define VT_OPENQRY    0x5600