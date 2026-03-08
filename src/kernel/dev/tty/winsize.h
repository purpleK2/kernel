#ifndef WINSIZE_H
#define WINSIZE_H 1

typedef struct winsize {
	unsigned short	ws_row;
	unsigned short	ws_col;
	unsigned short	ws_xpixel;
	unsigned short	ws_ypixel;
} winsize_t;

#define	TIOCGETA 19
#define	TIOCSETA 20
#define	TIOCSETAW 21
#define	TIOCSETAF 22

#define	TIOCGPGRP 119
#define	TIOCSPGRP 118

#define	TIOCGWINSZ 104
#define	TIOCSWINSZ 103

#define	TIOCSCTTY  0x540E
#define	TIOCNOTTY  0x5422

#define IOCTLTTYIS 0x8002

#endif // WINSIZE_H