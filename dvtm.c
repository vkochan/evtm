/*
 * The initial "port" of dwm to curses was done by
 *
 * © 2007-2016 Marc André Tanner <mat at brain-dump dot org>
 *
 * It is highly inspired by the original X11 dwm and
 * reuses some code of it which is mostly
 *
 * © 2006-2007 Anselm R. Garbe <garbeam at gmail dot com>
 *
 * See LICENSE for details.
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <wchar.h>
#include <limits.h>
#include <libgen.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <curses.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <pwd.h>
#if defined __CYGWIN__ || defined __sun
# include <termios.h>
#endif
#include "vt.h"

#ifdef PDCURSES
int ESCDELAY;
#endif

#ifndef NCURSES_REENTRANT
# define set_escdelay(d) (ESCDELAY = (d))
#endif

typedef struct {
	int history;
	int w;
	int h;
	volatile sig_atomic_t need_resize;
} Screen;

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Client Client;
struct Client {
	WINDOW *window;
	Vt *term;
	Vt *overlay, *app;
	bool is_editor;
	int editor_fds[2];
	volatile sig_atomic_t overlay_died;
	const char *cmd;
	char title[255];
	bool sync_title;
	int order;
	pid_t pid;
	unsigned short int id;
	unsigned short int x;
	unsigned short int y;
	unsigned short int w;
	unsigned short int h;
	bool has_title_line;
	bool minimized;
	bool urgent;
	volatile sig_atomic_t died;
	Client *next;
	Client *prev;
	Client *snext;
	unsigned int tags;
};

typedef struct {
	short fg;
	short bg;
	short fg256;
	short bg256;
	short pair;
} Color;

typedef struct {
	const char *title;
	attr_t attrs;
	Color *color;
} ColorRule;

/* #define ALT(k)      ((k) + (161 - 'a')) */
#define ALT	27
#if defined CTRL && defined _AIX
  #undef CTRL
#endif
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

#define MAX_ARGS 8

typedef struct {
	void (*cmd)(const char *args[]);
	const char *args[3];
} Action;

#define MAX_KEYS 3

typedef unsigned int KeyCombo[MAX_KEYS];

typedef struct {
	KeyCombo keys;
	Action action;
} KeyBinding;

typedef struct {
	mmask_t mask;
	Action action;
} Button;

typedef struct {
	const char *name;
	Action action;
} Cmd;

enum { BAR_TOP, BAR_BOTTOM, BAR_OFF };
enum { BAR_LEFT, BAR_RIGHT };

enum { MIN_ALIGN_HORIZ, MIN_ALIGN_VERT };

typedef struct {
	int fd;
	int pos, lastpos;
	int align;
	bool autohide;
	unsigned short int h;
	unsigned short int y;
	char text[512];
	const char *file;
} StatusBar;

typedef struct {
	int fd;
	const char *file;
	unsigned short int id;
} Fifo;

typedef struct {
	char *data;
	size_t len;
	size_t size;
} Register;

typedef struct {
	char *name;
	const char *argv[4];
	bool filter;
	bool color;
} Editor;

#define LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define MIN(x, y)   ((x) < (y) ? (x) : (y))
#define TAGMASK     ((1 << LENGTH(tags)) - 1)

#ifdef NDEBUG
 #define debug(format, args...)
#else
 #define debug eprint
#endif

/* commands for use by keybindings */
static void create(const char *args[]);
static void editor(const char *args[]);
static void copymode(const char *args[]);
static void copybuf(const char *args[]);
static void sendtext(const char *args[]);
static void capture(const char *args[]);
static void focusn(const char *args[]);
static void focusid(const char *args[]);
static void focusnext(const char *args[]);
static void focusnextnm(const char *args[]);
static void focusprev(const char *args[]);
static void focusprevnm(const char *args[]);
static void focuslast(const char *args[]);
static void focusup(const char *args[]);
static void focusdown(const char *args[]);
static void focusleft(const char *args[]);
static void focusright(const char *args[]);
static void togglesticky(const char *args[]);
static void setsticky(const char *args[]);
static void killclient(const char *args[]);
static void killother(const char *args[]);
static void paste(const char *args[]);
static void quit(const char *args[]);
static void redraw(const char *args[]);
static void scrollback(const char *args[]);
static void send(const char *args[]);
static void setlayout(const char *args[]);
static void togglemaximize(const char *args[]);
static void incnmaster(const char *args[]);
static int getnmaster(void);
static void setmfact(const char *args[]);
static float getmfact(void);
static void startup(const char *args[]);
static void tag(const char *args[]);
static void tagid(const char *args[]);
static void tagname(const char *args[]);
static void tagnamebycwd(const char *args[]);
static void togglebar(const char *args[]);
static void togglebarpos(const char *args[]);
static void toggleminimize(const char *args[]);
static void minimizeother(const char *args[]);
static void togglemouse(const char *args[]);
static void togglerunall(const char *args[]);
static void toggletag(const char *args[]);
static void toggleview(const char *args[]);
static void viewprevtag(const char *args[]);
static void view(const char *args[]);
static void zoom(const char *args[]);
static void setcwd(const char *args[]);
static void senduserevt(const char *args[]);
static void sendevtfmt(const char *fmt, ... );
static void docmd(const char *args[]);
static void doexec(const char *args[]);
static void doret(const char *msg, size_t len);
static void setstatus(const char *args[]);
static void setminimized(const char *args[]);

/* commands for use by mouse bindings */
static void mouse_focus(const char *args[]);
static void mouse_fullscreen(const char *args[]);
static void mouse_minimize(const char *args[]);
static void mouse_zoom(const char *args[]);

/* functions and variables available to layouts via config.h */
static void attachafter(Client *c, Client *a);
static Client* nextvisible(Client *c);

static void focus(Client *c);
static void resize(Client *c, int x, int y, int w, int h);
extern Screen screen;
static unsigned int waw, wah, wax, way;
static Client *clients = NULL;
static char *title;
static bool show_tagnamebycwd = false;

static KeyBinding *usrkeyb = NULL;
static int usrkeybn;

static KeyBinding *modkeyb = NULL;
static int modkeybn;

#include "config.h"

#define CWD_MAX		256

typedef struct {
	unsigned int curtag, prevtag;
	int nmaster[LENGTH(tags) + 1];
	float mfact[LENGTH(tags) + 1];
	Layout *layout[LENGTH(tags) + 1];
	Layout *layout_prev[LENGTH(tags) + 1];
	int barpos[LENGTH(tags) + 1];
	int barlastpos[LENGTH(tags) + 1];
	bool runinall[LENGTH(tags) + 1];
	char *cwd[LENGTH(tags) + 1];
	char *name[LENGTH(tags) + 1];
	bool msticky[LENGTH(tags) + 1];
} Pertag;

/* global variables */
static const char *dvtm_name = "dvtm";
Screen screen = { .history = SCROLL_HISTORY };
static Pertag pertag;
static Client *stack = NULL;
static Client *sel = NULL;
static Client *lastsel = NULL;
static Client *msel = NULL;
static unsigned int seltags;
static unsigned int tagset[2] = { 1, 1 };
static bool mouse_events_enabled = ENABLE_MOUSE;
static Layout *layout = layouts;
static StatusBar bar = { .fd = -1,
			 .lastpos = BAR_POS,
			 .pos = BAR_POS,
			 .align = BAR_RIGHT,
			 .autohide = BAR_AUTOHIDE,
			 .h = 1
		       };
static Fifo cmdfifo = { .fd = -1 };
static Fifo evtfifo = { .fd = -1 };
static Fifo cpyfifo = { .fd = -1 };
static Fifo retfifo = { .fd = -1 };
static const char *shell;
static Register copyreg;
static volatile sig_atomic_t running = true;
static bool runinall = false;
/* make sense only in layouts which has master window (tile, bstack) */
static int min_align = MIN_ALIGN_HORIZ;

static void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

static void
error(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static bool
isarrange(void (*func)()) {
	return func == layout->arrange;
}

static bool
isvisible(Client *c) {
	return c->tags & tagset[seltags];
}

static bool
is_content_visible(Client *c) {
	if (!c)
		return false;
	if (isarrange(fullscreen))
		return sel == c;
	return isvisible(c) && !c->minimized;
}

static Client*
nextvisible(Client *c) {
	for (; c && !isvisible(c); c = c->next);
	return c;
}

static bool ismastersticky(Client *c) {
	int n = 0;
	Client *m;

	if (!pertag.msticky[pertag.curtag])
		return false;
	if (!c)
		return true;

	for (m = nextvisible(clients); m && n < getnmaster(); m = nextvisible(m->next), n++) {
		if (c == m)
			return true;
	}

	return false;
}

static void
updatebarpos(void) {
	bar.y = 0;
	wax = 0;
	way = 0;
	wah = screen.h;
	waw = screen.w;
	if (bar.pos == BAR_TOP) {
		wah -= bar.h;
		way += bar.h;
	} else if (bar.pos == BAR_BOTTOM) {
		wah -= bar.h;
		bar.y = wah;
	}
}

static void
hidebar(void) {
	if (bar.pos != BAR_OFF) {
		bar.lastpos = pertag.barlastpos[pertag.curtag] = bar.pos;
		bar.pos = pertag.barpos[pertag.curtag] = BAR_OFF;
	}
}

static void
showbar(void) {
	if (bar.pos == BAR_OFF)
		bar.pos = pertag.barpos[pertag.curtag] = bar.lastpos;
}

static void
drawbar(void) {
	char buf[128];
	int sx, sy, x, y, width;
	unsigned int occupied = 0, urgent = 0;
	if (bar.pos == BAR_OFF)
		return;

	for (Client *c = clients; c; c = c->next) {
		occupied |= c->tags;
		if (c->urgent)
			urgent |= c->tags;
	}

	getyx(stdscr, sy, sx);
	attrset(BAR_ATTR);
	move(bar.y, 0);

	for (unsigned int i = 0; i < LENGTH(tags); i++){
		if (tagset[seltags] & (1 << i))
			attrset(TAG_SEL);
		else if (urgent & (1 << i))
			attrset(TAG_URGENT);
		else if (occupied & (1 << i))
			attrset(TAG_OCCUPIED);
		else
			attrset(TAG_NORMAL);

		if (pertag.name[i+1] && strlen(pertag.name[i+1])) {
			printw(TAG_SYMBOL, tags[i], ":", pertag.name[i+1]);
		} else if (strlen(pertag.cwd[i+1]) && show_tagnamebycwd && (occupied & (1 << i))) {
			printw(TAG_SYMBOL, tags[i], ":", basename(pertag.cwd[i+1]));
		} else {
			printw(TAG_SYMBOL, tags[i], "", "");
		}
	}

	attrset(pertag.runinall[pertag.curtag] ? TAG_SEL : TAG_NORMAL);
	addstr(layout->symbol);
	attrset(TAG_NORMAL);

	getyx(stdscr, y, x);
	(void)y;
	int maxwidth = screen.w - x - 2;

	addch(BAR_BEGIN);
	attrset(BAR_ATTR);

	wchar_t wbuf[sizeof bar.text];
	size_t numchars = mbstowcs(wbuf, bar.text, sizeof bar.text);

	if (numchars != (size_t)-1 && (width = wcswidth(wbuf, maxwidth)) != -1) {
		int pos = 0;

		if (bar.align == BAR_RIGHT) {
			for (; pos + width < maxwidth; pos++)
				addch(' ');
		}

		for (size_t i = 0; i < numchars; i++) {
			pos += wcwidth(wbuf[i]);
			if (pos > maxwidth)
				break;
			addnwstr(wbuf+i, 1);
		}

		if (bar.align == BAR_LEFT) {
			for (; pos + width < maxwidth; pos++)
				addch(' ');
		}

		clrtoeol();
	}

	attrset(TAG_NORMAL);
	mvaddch(bar.y, screen.w - 1, BAR_END);
	attrset(NORMAL_ATTR);
	move(sy, sx);
	wnoutrefresh(stdscr);
}

static int
show_border(void) {
	return (bar.pos != BAR_OFF) || (clients && clients->next);
}

static void
draw_border(Client *c) {
	int attrs_title = (COLOR(MAGENTA) | A_NORMAL);
	int x, y, maxlen, attrs = NORMAL_ATTR;
	char t = '\0';


	if (!show_border())
		return;
	if (sel != c && c->urgent)
		attrs = URGENT_ATTR;
	if (sel == c || (pertag.runinall[pertag.curtag] && !c->minimized))
		attrs_title = attrs = SELECTED_ATTR;

	wattrset(c->window, attrs);
	getyx(c->window, y, x);
	if (c == sel) {
	        wattrset(c->window, COLOR(BLUE_BG));
		mvwhline(c->window, 0, 0, ' ', c->w);
	        wattrset(c->window, attrs);
	} else {
		mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	}
	maxlen = c->w - 10;
	if (maxlen < 0)
		maxlen = 0;
	if ((size_t)maxlen < sizeof(c->title)) {
		t = c->title[maxlen];
		c->title[maxlen] = '\0';
	}

	wattrset(c->window, attrs_title);
	mvwprintw(c->window, 0, 2, "[%d|%s%s]",
		  c->order,
		  ismastersticky(c) ? "*" : "",
	          *c->title ? c->title : "");
	wattrset(c->window, attrs);
	if (t)
		c->title[maxlen] = t;
	wmove(c->window, y, x);
}

static void
draw_content(Client *c) {
	vt_draw(c->term, c->window, c->has_title_line, 0);
}

static void
draw(Client *c) {
	if (is_content_visible(c)) {
		redrawwin(c->window);
		draw_content(c);
	}
	if (!isarrange(fullscreen) || c == sel)
		draw_border(c);
	wnoutrefresh(c->window);
}

static void
draw_all(void) {
	if (!nextvisible(clients)) {
		sel = NULL;
		curs_set(0);
		erase();
		drawbar();
		doupdate();
		return;
	}

	if (!isarrange(fullscreen)) {
		for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c != sel)
				draw(c);
		}
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	if (sel)
		draw(sel);
}

static void
arrange(void) {
	unsigned int m = 0, n = 0, dh = 0;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		c->order = ++n;
		if (c->minimized)
			m++;
	}
	erase();
	attrset(NORMAL_ATTR);
	if (bar.fd == -1 && bar.autohide) {
		if ((!clients || !clients->next) && n == 1)
			hidebar();
		else
			showbar();
		updatebarpos();
	}
	if (m && !isarrange(fullscreen)) {
		if (min_align == MIN_ALIGN_VERT)
			dh = m;
		else
			dh = 1;
	}
	wah -= dh;
	layout->arrange();
	if (m && !isarrange(fullscreen)) {
		unsigned int i = 0, nw = waw / m, nx = wax;
		for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c->minimized) {
				if (min_align == MIN_ALIGN_VERT) {
					resize(c, nx, way+wah+i, waw, 1);
					i++;
				} else {
					resize(c, nx, way+wah, ++i == m ? waw - nx : nw, 1);
					nx += nw;
				}
			}
		}
		wah += dh;
	}
	focus(NULL);
	wnoutrefresh(stdscr);
	drawbar();
	draw_all();
}

static Client *
lastmaster(unsigned int tag) {
	Client *c = clients;
	int n = 1;

	for (; c && !(c->tags & tag); c = c->next);
	for (; c && n < getnmaster(); c = c->next, n++);

	return c;
}

static void
attachfirst(Client *c) {
	if (clients)
		clients->prev = c;
	c->next = clients;
	c->prev = NULL;
	clients = c;
	for (int o = 1; c; c = nextvisible(c->next), o++)
		c->order = o;
}

static void
attach(Client *c) {
	if (ismastersticky(NULL)) {
		Client *master = lastmaster(c->tags);

		if (master) {
			attachafter(c, master);
			return;
		}
	}

	attachfirst(c);
}

static void
attachafter(Client *c, Client *a) { /* attach c after a */
	if (c == a)
		return;
	if (!a)
		for (a = clients; a && a->next; a = a->next);

	if (a) {
		if (a->next)
			a->next->prev = c;
		c->next = a->next;
		c->prev = a;
		a->next = c;
		for (int o = a->order; c; c = nextvisible(c->next))
			c->order = ++o;
	}
}

static void
attachstack(Client *c) {
	c->snext = stack;
	stack = c;
}

static void
detach(Client *c) {
	Client *d;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next) {
		c->next->prev = c->prev;
		for (d = nextvisible(c->next); d; d = nextvisible(d->next))
			--d->order;
	}
	if (c == clients)
		clients = c->next;
	c->next = c->prev = NULL;
}

static void
settitle(Client *c) {
	char *term, *t = title;
	if (!t && sel == c && *c->title)
		t = c->title;
	if (t && (term = getenv("TERM")) && !strstr(term, "linux")) {
		printf("\033]0;%s\007", t);
		fflush(stdout);
	}
}

static void
detachstack(Client *c) {
	Client **tc;
	for (tc = &stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;
}

static void
focus(Client *c) {
	if (!c)
		for (c = stack; c && !isvisible(c); c = c->snext);

	if (c) {
		if (c->minimized) {
			modkeybn = LENGTH(min_bindings);
			modkeyb = min_bindings;
		} else {
			modkeybn = 0;
			modkeyb = NULL;
		}
	}

	if (sel == c)
		return;
	lastsel = sel;
	sel = c;
	if (lastsel) {
		lastsel->urgent = false;
		if (!isarrange(fullscreen)) {
			draw_border(lastsel);
			wnoutrefresh(lastsel->window);
		}
	}

	if (c) {
		detachstack(c);
		attachstack(c);
		settitle(c);
		c->urgent = false;
		if (isarrange(fullscreen)) {
			draw(c);
		} else {
			draw_border(c);
			wnoutrefresh(c->window);
		}
	}
	curs_set(c && !c->minimized && vt_cursor_visible(c->term));
}

static void
applycolorrules(Client *c) {
	const ColorRule *r = colorrules;
	short fg = r->color->fg, bg = r->color->bg;
	attr_t attrs = r->attrs;

	for (unsigned int i = 1; i < LENGTH(colorrules); i++) {
		r = &colorrules[i];
		if (strstr(c->title, r->title)) {
			attrs = r->attrs;
			fg = r->color->fg;
			bg = r->color->bg;
			break;
		}
	}

	vt_default_colors_set(c->term, attrs, fg, bg);
}

static void
term_title_handler(Vt *term, const char *title) {
	Client *c = (Client *)vt_data_get(term);
	if (title)
		strncpy(c->title, title, sizeof(c->title) - 1);
	c->title[title ? sizeof(c->title) - 1 : 0] = '\0';
	c->sync_title = false;
	settitle(c);
	if (!isarrange(fullscreen))
		draw_border(c);
	applycolorrules(c);
}

static void
term_urgent_handler(Vt *term) {
	Client *c = (Client *)vt_data_get(term);
	c->urgent = true;
	printf("\a");
	fflush(stdout);
	drawbar();
	if (!isarrange(fullscreen) && sel != c && isvisible(c))
		draw_border(c);
}

static void
move_client(Client *c, int x, int y) {
	if (c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR) {
		eprint("error moving, x: %d y: %d\n", x, y);
	} else {
		c->x = x;
		c->y = y;
	}
}

static void
resize_client(Client *c, int w, int h) {
	bool has_title_line = show_border();
	bool resize_window = c->w != w || c->h != h;
	if (resize_window) {
		debug("resizing, w: %d h: %d\n", w, h);
		if (wresize(c->window, h, w) == ERR) {
			eprint("error resizing, w: %d h: %d\n", w, h);
		} else {
			c->w = w;
			c->h = h;
		}
	}
	if (resize_window || c->has_title_line != has_title_line) {
		c->has_title_line = has_title_line;
		vt_resize(c->app, h - has_title_line, w);
		if (c->overlay)
			vt_resize(c->overlay, h - has_title_line, w);
	}
}

static void
resize(Client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

static Client*
get_client_by_coord(unsigned int x, unsigned int y) {
	if (y < way || y >= way+wah)
		return NULL;
	if (isarrange(fullscreen))
		return sel;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) {
			debug("mouse event, x: %d y: %d client: %d\n", x, y, c->order);
			return c;
		}
	}
	return NULL;
}

static void
sigchld_handler(int sig) {
	int errsv = errno;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD) {
				/* no more child processes */
				break;
			}
			eprint("waitpid: %s\n", strerror(errno));
			break;
		}

		debug("child with pid %d died\n", pid);

		for (Client *c = clients; c; c = c->next) {
			if (c->pid == pid) {
				c->died = true;
				break;
			}
			if (c->overlay && vt_pid_get(c->overlay) == pid) {
				c->overlay_died = true;
				break;
			}
		}
	}

	errno = errsv;
}

static void
sigwinch_handler(int sig) {
	screen.need_resize = true;
}

static void
sigterm_handler(int sig) {
	running = false;
}

static void
resize_screen(void) {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1) {
		getmaxyx(stdscr, screen.h, screen.w);
	} else {
		screen.w = ws.ws_col;
		screen.h = ws.ws_row;
	}

	debug("resize_screen(), w: %d h: %d\n", screen.w, screen.h);

	resizeterm(screen.h, screen.w);
	wresize(stdscr, screen.h, screen.w);
	updatebarpos();
	clear();
	arrange();
}

static KeyBinding*
keybindmatch(KeyBinding *keyb, unsigned int keybn, KeyCombo keys, unsigned int keycount) {
	for (unsigned int b = 0; b < keybn; b++) {
		for (unsigned int k = 0; k < keycount; k++) {
			if (keys[k] != keyb[b].keys[k])
				break;
			if (k == keycount - 1)
				return &keyb[b];
		}
	}
	return NULL;
}

static KeyBinding*
keybinding(KeyCombo keys, unsigned int keycount) {
	KeyBinding *keyb;

	keyb = keybindmatch(bindings, LENGTH(bindings), keys, keycount);
	if (!keyb && modkeyb)
		keyb = keybindmatch(modkeyb, modkeybn, keys, keycount);
	if (!keyb)
		keyb = keybindmatch(usrkeyb, usrkeybn, keys, keycount);
	return keyb;
}

static unsigned int
bitoftag(const char *tag) {
	unsigned int i;
	if (!tag)
		return ~0;
	for (i = 0; (i < LENGTH(tags)) && strcmp(tags[i], tag); i++);
	return (i < LENGTH(tags)) ? (1 << i) : 0;
}

static void
tagschanged() {
	bool allminimized = true;
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (!c->minimized) {
			allminimized = false;
			break;
		}
	}
	if (allminimized && nextvisible(clients)) {
		focus(NULL);
		toggleminimize(NULL);
	}
	arrange();
}

static void
tag(const char *args[]) {
	if (!sel)
		return;
	sel->tags = bitoftag(args[0]) & TAGMASK;
	tagschanged();
}

static void
tagid(const char *args[]) {
	if (!args[0] || !args[1])
		return;

	const int win_id = atoi(args[0]);
	for (Client *c = clients; c; c = c->next) {
		if (c->id == win_id) {
			unsigned int ntags = c->tags;
			for (unsigned int i = 1; i < MAX_ARGS && args[i]; i++) {
				if (args[i][0] == '+')
					ntags |= bitoftag(args[i]+1);
				else if (args[i][0] == '-')
					ntags &= ~bitoftag(args[i]+1);
				else
					ntags = bitoftag(args[i]);
			}
			ntags &= TAGMASK;
			if (ntags) {
				c->tags = ntags;
				tagschanged();
			}
			return;
		}
	}
}

static void
tagname(const char *args[]) {
	unsigned int tag_id;
	const char *name;

	if (!args[0])
		return;

	if (args[0] && args[1]) {
		tag_id = atoi(args[0]);
		if (tag_id >= LENGTH(tags))
			return;
	} else {
		tag_id = pertag.curtag;
	}

	free(pertag.name[tag_id]);
	pertag.name[tag_id] = NULL;

	name = args[0];
	if (args[1])
		name = args[1];

	if (name && strlen(name))
		pertag.name[tag_id] = strdup(name);
	drawbar();
}

static void
tagnamebycwd(const char *args[]) {
	if (!args || !args[0])
		return;

	if (strcmp(args[0], "on") == 0)
		show_tagnamebycwd = true;
	else if (strcmp(args[0], "off") == 0)
		show_tagnamebycwd = true;
	else
		return;

	drawbar();
}

static void
toggletag(const char *args[]) {
	if (!sel)
		return;
	unsigned int newtags = sel->tags ^ (bitoftag(args[0]) & TAGMASK);
	if (newtags) {
		sel->tags = newtags;
		tagschanged();
	}
}

static void
setpertag(void) {
	layout = pertag.layout[pertag.curtag];
	if (bar.pos != pertag.barpos[pertag.curtag]) {
		bar.pos = pertag.barpos[pertag.curtag];
		updatebarpos();
	}
	bar.lastpos = pertag.barlastpos[pertag.curtag];
	runinall = pertag.runinall[pertag.curtag];
	sendevtfmt("curtag %d\n", pertag.curtag);
}

static void
toggleview(const char *args[]) {
	int i;

	unsigned int newtagset = tagset[seltags] ^ (bitoftag(args[0]) & TAGMASK);
	if (newtagset) {
		if(newtagset == TAGMASK) {
			pertag.prevtag = pertag.curtag;
			pertag.curtag = 0;
		} else if(!(newtagset & 1 << (pertag.curtag - 1))) {
			pertag.prevtag = pertag.curtag;
			for (i=0; !(newtagset &1 << i); i++) ;
			pertag.curtag = i + 1;
		}
		setpertag();
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

static void
view(const char *args[]) {
	int i;

	unsigned int newtagset = bitoftag(args[0]) & TAGMASK;
	if (tagset[seltags] != newtagset && newtagset) {
		seltags ^= 1; /* toggle sel tagset */
		pertag.prevtag = pertag.curtag;
		if(args[0] == NULL)
			pertag.curtag = 0;
		else {
			for (i = 0; (i < LENGTH(tags)) && strcmp(tags[i], args[0]); i++) ;
			pertag.curtag = i + 1;
		}
		setpertag();
		tagset[seltags] = newtagset;
		tagschanged();
	}
}

static void
viewprevtag(const char *args[]) {
	unsigned int tmptag;

	seltags ^= 1;
	tmptag = pertag.prevtag;
	pertag.prevtag = pertag.curtag;
	pertag.curtag = tmptag;
	setpertag();
	tagschanged();
}

static void
keypress(int code) {
	int key = -1;
	unsigned int len = 1;
	char buf[8] = { '\e' };

	if (code == '\e') {
		/* pass characters following escape to the underlying app */
		nodelay(stdscr, TRUE);
		for (int t; len < sizeof(buf) && (t = getch()) != ERR; len++) {
			if (t > 255) {
				key = t;
				break;
			}
			buf[len] = t;
		}
		nodelay(stdscr, FALSE);
	}

	for (Client *c = pertag.runinall[pertag.curtag] ? nextvisible(clients) : sel; c; c = nextvisible(c->next)) {
		if (is_content_visible(c)) {
			c->urgent = false;
			if (code == '\e')
				vt_write(c->term, buf, len);
			else
				vt_keypress(c->term, code);
			if (key != -1)
				vt_keypress(c->term, key);
		}
		if (!pertag.runinall[pertag.curtag])
			break;
	}
}

static void
mouse_setup(void) {
#ifdef CONFIG_MOUSE
	mmask_t mask = 0;

	if (mouse_events_enabled) {
		mask = BUTTON1_CLICKED | BUTTON2_CLICKED;
		for (unsigned int i = 0; i < LENGTH(buttons); i++)
			mask |= buttons[i].mask;
	}
	mousemask(mask, NULL);
#endif /* CONFIG_MOUSE */
}

static void
initpertag(void) {
	int i;

	pertag.curtag = pertag.prevtag = 1;
	for(i=0; i <= LENGTH(tags); i++) {
		pertag.nmaster[i] = NMASTER;
		pertag.mfact[i] = MFACT;
		pertag.layout[i] = layout;
		pertag.layout_prev[i] = layout;
		pertag.barpos[i] = bar.pos;
		pertag.barlastpos[i] = bar.lastpos;
		pertag.runinall[i] = runinall;
		pertag.msticky[i] = false;
		pertag.name[i] = NULL;
		pertag.cwd[i] = calloc(CWD_MAX, 1);
	}
}

static bool
checkshell(const char *shell) {
	if (shell == NULL || *shell == '\0' || *shell != '/')
		return false;
	if (!strcmp(strrchr(shell, '/')+1, dvtm_name))
		return false;
	if (access(shell, X_OK))
		return false;
	return true;
}

static const char *
getshell(void) {
	const char *shell = getenv("SHELL");
	struct passwd *pw;

	if (checkshell(shell))
		return shell;
	if ((pw = getpwuid(getuid())) && checkshell(pw->pw_shell))
		return pw->pw_shell;
	return "/bin/sh";
}

static void
setup(void) {
	shell = getshell();
	setlocale(LC_CTYPE, "");
	initscr();
	start_color();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	mouse_setup();
	raw();
	vt_init();
	vt_keytable_set(keytable, LENGTH(keytable));
	for (unsigned int i = 0; i < LENGTH(colors); i++) {
		if (COLORS == 256) {
			if (colors[i].fg256)
				colors[i].fg = colors[i].fg256;
			if (colors[i].bg256)
				colors[i].bg = colors[i].bg256;
		}
		colors[i].pair = vt_color_reserve(colors[i].fg, colors[i].bg);
	}
	initpertag();
	resize_screen();
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigwinch_handler;
	sigaction(SIGWINCH, &sa, NULL);
	sa.sa_handler = sigchld_handler;
	sigaction(SIGCHLD, &sa, NULL);
	sa.sa_handler = sigterm_handler;
	sigaction(SIGTERM, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
}

static void
destroy(Client *c) {
	if (sel == c)
		focusnextnm(NULL);
	detach(c);
	detachstack(c);
	if (sel == c) {
		Client *next = nextvisible(clients);
		if (next) {
			focus(next);
			toggleminimize(NULL);
		} else {
			sel = NULL;
		}
	}
	if (lastsel == c)
		lastsel = NULL;
	werase(c->window);
	wnoutrefresh(c->window);
	vt_destroy(c->term);
	delwin(c->window);
	if (!clients && LENGTH(actions)) {
		if (!strcmp(c->cmd, shell))
			quit(NULL);
		else
			create(NULL);
	}
	free(c);
	arrange();
}

static void
cleanup(void) {
	int i;
	while (clients)
		destroy(clients);
	vt_shutdown();
	endwin();
	free(copyreg.data);
	if (bar.fd > 0)
		close(bar.fd);
	if (bar.file)
		unlink(bar.file);
	if (cmdfifo.fd > 0)
		close(cmdfifo.fd);
	if (cmdfifo.file)
		unlink(cmdfifo.file);
	if (evtfifo.fd > 0)
		close(evtfifo.fd);
	if (evtfifo.file)
		unlink(evtfifo.file);
	if (cpyfifo.fd > 0)
		close(cpyfifo.fd);
	if (cpyfifo.file)
		unlink(cpyfifo.file);
	if (retfifo.fd > 0)
		close(retfifo.fd);
	if (retfifo.file)
		unlink(retfifo.file);
	for(i=0; i <= LENGTH(tags); i++) {
		free(pertag.name[i]);
		free(pertag.cwd[i]);
	}
	for(i=0; i < usrkeybn; i++)
		free((char *) usrkeyb[i].action.args[0]);
	free(usrkeyb);
}

static char *getcwd_by_pid(Client *c) {
	if (!c)
		return NULL;
	char buf[32];
	snprintf(buf, sizeof buf, "/proc/%d/cwd", c->pid);
	return realpath(buf, NULL);
}

static void
synctitle(Client *c)
{
	size_t len = sizeof(c->title);
	char buf[128];
	char path[64];
	size_t blen;
	char *eol;
	pid_t pid;
	int pty;
	int ret;
	int fd;

	pty = c->overlay ? vt_pty_get(c->overlay) : vt_pty_get(c->app);

	pid = tcgetpgrp(pty);
	if (pid == -1)
		return;

	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return;

	blen = MIN(sizeof(buf), sizeof(c->title));

	ret = read(fd, buf, blen);
	if (ret <= 0)
		goto done;

	buf[ret - 1] = '\0';

	strncpy(c->title, basename(buf), ret);

	settitle(c);
	if (!isarrange(fullscreen) || sel == c)
		draw_border(c);
done:
	close(fd);
}

static void
create(const char *args[]) {
	const char *pargs[4] = { shell, NULL };
	char buf[8], *cwd = NULL;
	const char *env[] = {
		"DVTM_WINDOW_ID", buf,
		NULL
	};

	if (args && args[0]) {
		pargs[1] = "-c";
		pargs[2] = args[0];
		pargs[3] = NULL;
	}
	Client *c = calloc(1, sizeof(Client));
	if (!c)
		return;
	c->tags = tagset[seltags];
	c->id = ++cmdfifo.id;
	snprintf(buf, sizeof buf, "%d", c->id);

	if (!(c->window = newwin(wah, waw, way, wax))) {
		free(c);
		return;
	}

	c->term = c->app = vt_create(screen.h, screen.w, screen.history);
	if (!c->term) {
		delwin(c->window);
		free(c);
		return;
	}

	if (args && args[0]) {
		c->cmd = args[0];
		char name[PATH_MAX];
		strncpy(name, args[0], sizeof(name));
		name[sizeof(name)-1] = '\0';
		strncpy(c->title, basename(name), sizeof(c->title));
	} else {
		c->cmd = shell;
	}

	if (args && args[1])
		strncpy(c->title, args[1], sizeof(c->title));
	c->title[sizeof(c->title)-1] = '\0';

	if (strlen(c->title) == 0)
		c->sync_title = true;
	else
		c->sync_title = false;

	if (args && args[2])
		cwd = !strcmp(args[2], "$CWD") ? getcwd_by_pid(sel) : (char*)args[2];
	else if (strlen(pertag.cwd[pertag.curtag]))
		cwd = pertag.cwd[pertag.curtag];

	c->pid = vt_forkpty(c->term, shell, pargs, cwd, env, NULL, NULL);
	if (args && args[2] && !strcmp(args[2], "$CWD"))
		free(cwd);

	vt_data_set(c->term, c);
	vt_title_handler_set(c->term, term_title_handler);
	vt_urgent_handler_set(c->term, term_urgent_handler);
	applycolorrules(c);
	c->x = wax;
	c->y = way;
	debug("client with pid %d forked\n", c->pid);
	attach(c);
	focus(c);
	arrange();
}

static void
editor(const char *args[]) {
	const char *editor[3] = { NULL };

	editor[0] = getenv("DVTM_EDITOR");
	if (!editor[0])
		editor[0] = getenv("VISUAL");
	if (!editor[0])
		editor[0] = getenv("EDITOR");
	if (!editor[0])
		editor[0] = "vi";

	create(editor);
}

static void
copymode(const char *args[]) {
	if (!args || !args[0] || !sel || sel->overlay)
		return;

	bool colored = strstr(args[0], "pager") != NULL;

	if (!(sel->overlay = vt_create(sel->h - sel->has_title_line, sel->w, 0)))
		return;

	int *to = &sel->editor_fds[0];
	int *from = strstr(args[0], "editor") ? &sel->editor_fds[1] : NULL;
	sel->editor_fds[0] = sel->editor_fds[1] = -1;

	const char *argv[3] = { args[0], NULL, NULL };
	char argline[32];
	int line = vt_content_start(sel->app);
	snprintf(argline, sizeof(argline), "+%d", line);
	argv[1] = argline;

	if (vt_forkpty(sel->overlay, args[0], argv, NULL, NULL, to, from) < 0) {
		vt_destroy(sel->overlay);
		sel->overlay = NULL;
		return;
	}

	sel->term = sel->overlay;

	if (sel->editor_fds[0] != -1) {
		char *buf = NULL;
		size_t len = vt_content_get(sel->app, &buf, colored);
		char *cur = buf;
		while (len > 0) {
			ssize_t res = write(sel->editor_fds[0], cur, len);
			if (res < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				break;
			}
			cur += res;
			len -= res;
		}
		free(buf);
		close(sel->editor_fds[0]);
		sel->editor_fds[0] = -1;
	}
	sel->is_editor = true;

	if (args[1])
		vt_write(sel->overlay, args[1], strlen(args[1]));
}

static void
copybuf(const char *args[]) {
	char buf[1024];
	int offs = 0;
	int len;

	if (!args || !args[0])
		return;

	if (strcmp(args[0], "put") == 0) {
		copyreg.len = 0;

		do {
			len = read(cpyfifo.fd, buf, sizeof(buf));
			if (len <= 0)
				break;

			if (!copyreg.data) {
				copyreg.data = malloc(sizeof(buf));
				copyreg.size = sizeof(buf);
			}

			copyreg.len += len;

			if (copyreg.len > copyreg.size) {
				copyreg.data = realloc(copyreg.data, copyreg.len);
				copyreg.size = copyreg.len;
			}

			memcpy(copyreg.data + offs, buf, len);
			offs += len;
		} while (len == sizeof(buf));
	} else if (strcmp(args[0], "get") == 0) {
		doret(copyreg.data, copyreg.len);
	}
}

static void
sendtext(const char *args[]) {
	char buf[512];
	int len;

	if (!sel || !args || !args[0])
		return;

	if (strcmp(args[0], "-") == 0) {
		do {
			len = read(cpyfifo.fd, buf, sizeof(buf));
			if (len <= 0)
				break;

			vt_write(sel->app, buf, len);
		} while (len == sizeof(buf));
	} else {
	    vt_write(sel->app, args[0], strlen(args[0]));
	}
}

static void
capture(const char *args[]) {
	char *buf = NULL;
	size_t len;

	if (!sel)
		return;

	len = vt_content_get(sel->app, &buf, false);
	doret(buf, len);
}

static void
focusn(const char *args[]) {
	for (Client *c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (c->order == atoi(args[0])) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

static void
focusid(const char *args[]) {
	if (!args[0])
		return;

	const int win_id = atoi(args[0]);
	for (Client *c = clients; c; c = c->next) {
		if (c->id == win_id) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			if (!isvisible(c)) {
				c->tags |= tagset[seltags];
				tagschanged();
			}
			return;
		}
	}
}

static void
focusnext(const char *args[]) {
	Client *c;
	if (!sel)
		return;
	for (c = sel->next; c && !isvisible(c); c = c->next);
	if (!c)
		for (c = clients; c && !isvisible(c); c = c->next);
	if (c)
		focus(c);
}

static void
focusnextnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		c = nextvisible(c->next);
		if (!c)
			c = nextvisible(clients);
	} while (c->minimized && c != sel);
	focus(c);
}

static void
focusprev(const char *args[]) {
	Client *c;
	if (!sel)
		return;
	for (c = sel->prev; c && !isvisible(c); c = c->prev);
	if (!c) {
		for (c = clients; c && c->next; c = c->next);
		for (; c && !isvisible(c); c = c->prev);
	}
	if (c)
		focus(c);
}

static void
focusprevnm(const char *args[]) {
	if (!sel)
		return;
	Client *c = sel;
	do {
		for (c = c->prev; c && !isvisible(c); c = c->prev);
		if (!c) {
			for (c = clients; c && c->next; c = c->next);
			for (; c && !isvisible(c); c = c->prev);
		}
	} while (c && c != sel && c->minimized);
	focus(c);
}

static void
focuslast(const char *args[]) {
	if (lastsel)
		focus(lastsel);
}

static void
focusup(const char *args[]) {
	if (!sel)
		return;
	/* avoid vertical separator, hence +1 in x direction */
	Client *c = get_client_by_coord(sel->x + 1, sel->y - 1);
	if (c)
		focus(c);
	else
		focusprev(args);
}

static void
focusdown(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x, sel->y + sel->h);
	if (c)
		focus(c);
	else
		focusnext(args);
}

static void
focusleft(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x - 2, sel->y);
	if (c)
		focus(c);
	else
		focusprev(args);
}

static void
focusright(const char *args[]) {
	if (!sel)
		return;
	Client *c = get_client_by_coord(sel->x + sel->w + 1, sel->y);
	if (c)
		focus(c);
	else
		focusnext(args);
}

static void
togglesticky(const char *args[]) {
	pertag.msticky[pertag.curtag] = !pertag.msticky[pertag.curtag];
	draw_all();
}

void static
setsticky(const char *args[]) {
	int tag = pertag.curtag;
	bool on = true;

	if (args && args[0]) {
		if (strncmp("on", args[0], 2) == 0)
			on = true;
		else if (strncmp("off", args[0], 3) == 0)
			on = false;
	}
	if (args && args[1]) {
		tag = bitoftag(args[1]) & TAGMASK;
	}

	pertag.msticky[tag] = on;
	draw_all();
}

static void
killclient(const char *args[]) {
	Client *target = sel;

	if (args && args[0]) {
		int order = atoi(args[0]);
		Client *c;

		for (c = nextvisible(clients); c; c = nextvisible(c->next)) {
			if (c->order == order) {
				target = c;
				break;
			}
		}
	}

	if (!target)
		return;

	debug("killing client with pid: %d\n", target->pid);
	kill(-target->pid, SIGKILL);
}

static void killother(const char *args[]) {
	unsigned int n;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (ismastersticky(c) || sel == c)
			continue;
		kill(-c->pid, SIGKILL);
	}
}

static void
paste(const char *args[]) {
	if (sel && copyreg.data) {
		size_t len = copyreg.len;

		if (copyreg.data[len - 2] == '\r')
			len--;
		if (copyreg.data[len - 1] == '\n')
			len--;
		vt_write(sel->term, copyreg.data, len);
	}
}

static void
quit(const char *args[]) {
	cleanup();
	exit(EXIT_SUCCESS);
}

static void
redraw(const char *args[]) {
	for (Client *c = clients; c; c = c->next) {
		if (!c->minimized) {
			vt_dirty(c->term);
			wclear(c->window);
			wnoutrefresh(c->window);
		}
	}
	resize_screen();
}

static void
scrollback(const char *args[]) {
	if (!is_content_visible(sel))
		return;

	if (!args[0] || atoi(args[0]) < 0)
		vt_scroll(sel->term, -sel->h/2);
	else
		vt_scroll(sel->term,  sel->h/2);

	draw(sel);
	curs_set(vt_cursor_visible(sel->term));
}

static void
send(const char *args[]) {
	if (sel && args && args[0])
		vt_write(sel->term, args[0], strlen(args[0]));
}

static void
setlayout(const char *args[]) {
	unsigned int i;

	if (!args || !args[0]) {
		if (++layout == &layouts[LENGTH(layouts)])
			layout = &layouts[0];
	} else {
		for (i = 0; i < LENGTH(layouts); i++)
			if (!strcmp(args[0], layouts[i].symbol))
				break;
		if (i == LENGTH(layouts))
			return;
		layout = &layouts[i];
	}
	pertag.layout_prev[pertag.curtag] = pertag.layout[pertag.curtag];
	pertag.layout[pertag.curtag] = layout;
	arrange();
}

static void
togglemaximize(const char *args[]) {
	if (isarrange(fullscreen)) {
		layout = pertag.layout_prev[pertag.curtag];
		pertag.layout[pertag.curtag] = layout;
		arrange();
	} else {
		const char *maxargs[] = { "[ ]" };
		setlayout(maxargs);
	}
}

static void
incnmaster(const char *args[]) {
	int delta, nmaster;

	if (isarrange(fullscreen) || isarrange(grid))
		return;

	nmaster = pertag.nmaster[pertag.curtag];

	/* arg handling, manipulate nmaster */
	if (args[0] == NULL) {
		nmaster = NMASTER;
	} else if (sscanf(args[0], "%d", &delta) == 1) {
		if (args[0][0] == '+' || args[0][0] == '-')
			nmaster += delta;
		else
			nmaster = delta;
		if (nmaster < 1)
			nmaster = 1;
	}
	pertag.nmaster[pertag.curtag] = nmaster;
	arrange();
}

static int
getnmaster(void) {
	return pertag.nmaster[pertag.curtag];
}

static void
setmfact(const char *args[]) {
	float delta, mfact;

	if (isarrange(fullscreen) || isarrange(grid))
		return;

	mfact = pertag.mfact[pertag.curtag];

	/* arg handling, manipulate mfact */
	if (args[0] == NULL) {
		mfact = MFACT;
	} else if (sscanf(args[0], "%f", &delta) == 1) {
		if (args[0][0] == '+' || args[0][0] == '-')
			mfact += delta;
		else
			mfact = delta;
		if (mfact < 0.1)
			mfact = 0.1;
		else if (mfact > 0.9)
			mfact = 0.9;
	}

	pertag.mfact[pertag.curtag] = mfact;
	arrange();
}

static float
getmfact(void) {
	return pertag.mfact[pertag.curtag];
}

static void
startup(const char *args[]) {
	for (unsigned int i = 0; i < LENGTH(actions); i++)
		actions[i].cmd(actions[i].args);
}

static void
togglebar(const char *args[]) {
	if (bar.pos == BAR_OFF)
		showbar();
	else
		hidebar();
	bar.autohide = false;
	updatebarpos();
	redraw(NULL);
}

static void
togglebarpos(const char *args[]) {
	switch (bar.pos == BAR_OFF ? bar.lastpos : bar.pos) {
	case BAR_TOP:
		bar.pos = pertag.barpos[pertag.curtag] = BAR_BOTTOM;
		break;
	case BAR_BOTTOM:
		bar.pos = pertag.barpos[pertag.curtag] = BAR_TOP;
		break;
	}
	updatebarpos();
	redraw(NULL);
}

static void
toggleminimize(const char *args[]) {
	Client *c, *m, *t;
	unsigned int n;
	if (!sel)
		return;
	/* do not minimize sticked master */
	if (ismastersticky(sel))
		return;
	/* the last window can't be minimized */
	if (!sel->minimized) {
		for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next))
			if (!c->minimized)
				n++;
		if (n == 1)
			return;
	}
	sel->minimized = !sel->minimized;
	m = sel;
	/* check whether the master client was minimized */
	if (sel == nextvisible(clients) && sel->minimized) {
		c = nextvisible(sel->next);
		detach(c);
		attach(c);
		focus(c);
		detach(m);
		for (; c && (t = nextvisible(c->next)) && !t->minimized; c = t);
		attachafter(m, c);
	} else if (m->minimized) {
		/* non master window got minimized move it above all other
		 * minimized ones */
		focusnextnm(NULL);
		detach(m);
		for (c = nextvisible(clients); c && (t = nextvisible(c->next)) && !t->minimized; c = t);
		attachafter(m, c);
	} else { /* window is no longer minimized, move it to the master area */
		vt_dirty(m->term);
		detach(m);
		attach(m);
	}
	arrange();
}

static void minimizeother(const char *args[])
{
	unsigned int n;
	Client *c;

	for (n = 0, c = nextvisible(clients); c; c = nextvisible(c->next)) {
		if (ismastersticky(c) || sel == c)
			continue;

		c->minimized = true;
	}

	arrange();
}

static void
togglemouse(const char *args[]) {
	mouse_events_enabled = !mouse_events_enabled;
	mouse_setup();
}

static void
togglerunall(const char *args[]) {
	pertag.runinall[pertag.curtag] = !pertag.runinall[pertag.curtag];
	drawbar();
	draw_all();
}

static void
zoom(const char *args[]) {
	Client *c;

	if (!sel)
		return;
	if (args && args[0])
		focusn(args);
	if ((c = sel) == nextvisible(clients))
		if (!(c = nextvisible(c->next)))
			return;
	detach(c);
	attachfirst(c);
	focus(c);
	if (c->minimized)
		toggleminimize(NULL);
	arrange();
}

static void
setcwd(const char *args[]) {
	int tag = pertag.curtag;

	if (!args || !args[0])
		return;
	if (args && args[1])
		tag = atoi(args[0]);

	strncpy(pertag.cwd[tag], args[0], CWD_MAX - 1);

	/* in case tagnamebydir is set */
	drawbar();
}

static void senduserevt(const char *args[]) {
	if (!args || !args[0] || !args[1])
		return;

	write(evtfifo.fd, args[0], (size_t)args[1]);
}

void sendevtfmt(const char *fmt, ... )
{
	char buf[256];
	va_list args;
	int len;

	if (evtfifo.fd == -1)
		return;

	va_start (args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	write(evtfifo.fd, buf, len);
	va_end (args);
}

/* commands for use by mouse bindings */
static void
mouse_focus(const char *args[]) {
	focus(msel);
	if (msel->minimized)
		toggleminimize(NULL);
}

static void
mouse_fullscreen(const char *args[]) {
	mouse_focus(NULL);
	setlayout(isarrange(fullscreen) ? NULL : args);
}

static void
mouse_minimize(const char *args[]) {
	focus(msel);
	toggleminimize(NULL);
}

static void
mouse_zoom(const char *args[]) {
	focus(msel);
	zoom(NULL);
}

static Cmd *
get_cmd_by_name(const char *name) {
	for (unsigned int i = 0; i < LENGTH(commands); i++) {
		if (!strcmp(name, commands[i].name))
			return &commands[i];
	}
	return NULL;
}

static void
handle_cmd(char *cmdbuf) {
	char *p, *s, c;
	Cmd *cmd;

	p = cmdbuf;
	while (*p) {
		/* find the command name */
		for (; *p == ' ' || *p == '\n'; p++);
		for (s = p; *p && *p != ' ' && *p != '\n'; p++);
		if ((c = *p))
			*p++ = '\0';
		if (*s && (cmd = get_cmd_by_name(s)) != NULL) {
			bool quote = false;
			int argc = 0;
			const char *args[MAX_ARGS], *arg;
			memset(args, 0, sizeof(args));
			/* if arguments were specified in config.h ignore the one given via
			 * the named pipe and thus skip everything until we find a new line
			 */
			if (cmd->action.args[0] || c == '\n') {
				debug("execute %s", s);
				cmd->action.cmd(cmd->action.args);
				while (*p && *p != '\n')
					p++;
				continue;
			}
			/* no arguments were given in config.h so we parse the command line */
			while (*p == ' ')
				p++;
			arg = p;
			for (; (c = *p); p++) {
				switch (*p) {
				case '\\':
					/* remove the escape character '\\' move every
					 * following character to the left by one position
					 */
					switch (p[1]) {
						case '\\':
						case '\'':
						case '\"': {
							char *t = p+1;
							do {
								t[-1] = *t;
							} while (*t++);
						}
					}
					break;
				case '\'':
				case '\"':
					quote = !quote;
					break;
				case ' ':
					if (!quote) {
				case '\n':
						/* remove trailing quote if there is one */
						if (*(p - 1) == '\'' || *(p - 1) == '\"')
							*(p - 1) = '\0';
						*p++ = '\0';
						/* remove leading quote if there is one */
						if (*arg == '\'' || *arg == '\"')
							arg++;
						if (argc < MAX_ARGS)
							args[argc++] = arg;

						while (*p == ' ')
							++p;
						arg = p--;
					}
					break;
				}

				if (c == '\n' || *p == '\n') {
					if (!*p)
						p++;
					debug("execute %s", s);
					for(int i = 0; i < argc; i++)
						debug(" %s", args[i]);
					debug("\n");
					cmd->action.cmd(args);
					break;
				}
			}
		}
	}
}

static void
handle_cmdfifo(void) {
	char *p, cmdbuf[512];
	int r;

	r = read(cmdfifo.fd, cmdbuf, sizeof cmdbuf - 1);
	if (r <= 0) {
		cmdfifo.fd = -1;
		return;
	}

	cmdbuf[r] = '\0';
	handle_cmd(cmdbuf);
}

static void docmd(const char *args[]) {
	char cmdbuf[512];

	if (!args || !args[0])
		return;

	strncpy(cmdbuf, args[0], sizeof(cmdbuf) - 1);
	handle_cmd(cmdbuf);
}

static void doexec(const char *args[]) {
	if (!args || !args[0] || sel->overlay)
		return;

	if (!(sel->overlay = vt_create(sel->h - sel->has_title_line, sel->w, 0)))
		return;

	if (vt_forkpty(sel->overlay, args[0], args, NULL, NULL, NULL, NULL) < 0) {
		vt_destroy(sel->overlay);
		sel->overlay = NULL;
		return;
	}

	sel->term = sel->overlay;
}

static void doret(const char *msg, size_t len) {
    char tmp[10] = { '\n' };
    unsigned int lines = 1;
    const char *ptr = msg;
    int i;

    if (retfifo.fd <= 0)
	    return;

    for (i = 0; i < len; i++, ptr++)
	    if (*ptr == '\n')
		    lines++;

    write(retfifo.fd, tmp, snprintf(tmp, sizeof(tmp), "%u\n", lines));
    write(retfifo.fd, msg, len);
    write(retfifo.fd, "\n", 1);
}

static void setstatus(const char *args[]) {
	if (!args || !args[0] || !args[1])
		return;

	if (strcmp("align", args[0]) == 0) {
		if (strcmp("left", args[1]) == 0)
			bar.align = BAR_LEFT;
		else if (strcmp("right", args[1]) == 0)
			bar.align = BAR_RIGHT;
	}
}

static void setminimized(const char *args[]) {
	if (!args || !args[0] || !args[1])
		return;

	if (strcmp("align", args[0]) == 0) {
		if (strcmp("vert", args[1]) == 0)
			min_align = MIN_ALIGN_VERT;
		else if (strcmp("horiz", args[1]) == 0)
			min_align = MIN_ALIGN_HORIZ;
	}
}

static void
handle_mouse(void) {
#ifdef CONFIG_MOUSE
	MEVENT event;
	unsigned int i;
	if (getmouse(&event) != OK)
		return;
	msel = get_client_by_coord(event.x, event.y);

	if (!msel)
		return;

	debug("mouse x:%d y:%d cx:%d cy:%d mask:%d\n", event.x, event.y, event.x - msel->x, event.y - msel->y, event.bstate);

	vt_mouse(msel->term, event.x - msel->x, event.y - msel->y, event.bstate);

	for (i = 0; i < LENGTH(buttons); i++) {
		if (event.bstate & buttons[i].mask)
			buttons[i].action.cmd(buttons[i].action.args);
	}

	msel = NULL;
#endif /* CONFIG_MOUSE */
}

static void
handle_statusbar(void) {
	char *p;
	int r;
	switch (r = read(bar.fd, bar.text, sizeof bar.text - 1)) {
		case -1:
			strncpy(bar.text, strerror(errno), sizeof bar.text - 1);
			bar.text[sizeof bar.text - 1] = '\0';
			bar.fd = -1;
			break;
		case 0:
			bar.fd = -1;
			break;
		default:
			bar.text[r] = '\0';
			p = bar.text + r - 1;
			for (; p >= bar.text && *p == '\n'; *p-- = '\0');
			for (; p >= bar.text && *p != '\n'; --p);
			if (p >= bar.text)
				memmove(bar.text, p + 1, strlen(p));
			drawbar();
	}
}

static void
handle_editor(Client *c) {
	if (!copyreg.data && (copyreg.data = malloc(screen.history)))
		copyreg.size = screen.history;
	copyreg.len = 0;
	while (c->editor_fds[1] != -1 && copyreg.len < copyreg.size) {
		ssize_t len = read(c->editor_fds[1], copyreg.data + copyreg.len, copyreg.size - copyreg.len);
		if (len == -1) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (len == 0)
			break;
		copyreg.len += len;
		if (copyreg.len == copyreg.size) {
			copyreg.size *= 2;
			if (!(copyreg.data = realloc(copyreg.data, copyreg.size))) {
				copyreg.size = 0;
				copyreg.len = 0;
			}
		}
	}
}

static void
handle_overlay(Client *c) {
	if (c->is_editor)
		handle_editor(c);

	c->overlay_died = false;
	c->is_editor = false;
	c->editor_fds[1] = -1;
	vt_destroy(c->overlay);
	c->overlay = NULL;
	c->term = c->app;
	vt_dirty(c->term);
	draw_content(c);
	wnoutrefresh(c->window);
}

static int
__open_or_create_fifo(const char *name, const char **name_created, int flags) {
	struct stat info;
	int fd;

	do {
		if ((fd = open(name, flags)) == -1) {
			if (errno == ENOENT && !mkfifo(name, S_IRUSR|S_IWUSR)) {
				*name_created = name;
				continue;
			}
			error("%s\n", strerror(errno));
		}
	} while (fd == -1);

	if (fstat(fd, &info) == -1)
		error("%s\n", strerror(errno));
	if (!S_ISFIFO(info.st_mode))
		error("%s is not a named pipe\n", name);
	return fd;
}

static int
open_or_create_fifo(const char *name, const char **name_created) {
	return __open_or_create_fifo(name, name_created, O_RDWR|O_NONBLOCK);
}

static void
usage(void) {
	cleanup();
	eprint("usage: dvtm [-v] [-M] [-m mod] [-d delay] [-h lines] [-t title] "
	       "[-s status-fifo] [-c cmd-fifo] [cmd...]\n");
	exit(EXIT_FAILURE);
}

static void
addusrkeyb(char *map)
{
	char *keystr, *typestr, *valstr;
	KeyBinding *key;
	char *keybval;
	size_t vlen;
	int i;

	keystr = map;

	typestr = strchr(keystr, ':');
	if (!typestr)
		error("missing key-bind type %s\n", map);
	*typestr = '\0';
	typestr++;

	valstr = strchr(typestr, ':');
	if (!valstr)
		error("missing key-bind value %s\n", map);
	*valstr = '\0';
	valstr++;

	if (strcmp(typestr, "m") != 0 && strcmp(typestr, "x") != 0 && strcmp(typestr, "c") != 0)
		error("invalid key-bind type %s\n", typestr);
	if (strlen(valstr) == 0)
		error("key-bind value is empty\n");

	usrkeybn++;
	usrkeyb = realloc(usrkeyb, sizeof(*usrkeyb) * usrkeybn);
	if (!usrkeyb)
		error("fail on usrkeyb realloc\n");

	key = &usrkeyb[usrkeybn - 1];
	memset(key, 0, sizeof(*key));

	for (i = 0; i < MAX_KEYS; i++) {
		key->keys[i] = *keystr++;
		if (key->keys[i] == '^') {
			key->keys[i] = CTRL(*keystr);
			keystr++;
		}
	}
	keybval = strdup(valstr);
	if (!keybval)
		error("fail on key-bind value dup\n");
	vlen = strlen(keybval);
	key->action.args[0] = keybval;
	if (strcmp(typestr, "m") == 0) {
		key->action.args[1] = (const char *)(vlen + 1);
		key->action.cmd = senduserevt;
		keybval[vlen] = '\n';
	} else if (strcmp(typestr, "x") == 0) {
		key->action.cmd = create;
	} else if (strcmp(typestr, "c") == 0) {
		key->action.cmd = docmd;
		keybval[vlen] = '\n';
	}
}

static bool
parse_args(int argc, char *argv[]) {
	bool init = false;
	const char *name = argv[0];

	if (name && (name = strrchr(name, '/')))
		dvtm_name = name + 1;
	if (!getenv("ESCDELAY"))
		set_escdelay(100);
	for (int arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-') {
			const char *args[] = { argv[arg], NULL, NULL };
			if (!init) {
				setup();
				init = true;
			}
			create(args);
			continue;
		}
		if (argv[arg][1] != 'v' && argv[arg][1] != 'M' && (arg + 1) >= argc)
			usage();
		switch (argv[arg][1]) {
			case 'v':
				puts("dvtm-"VERSION" © 2007-2016 Marc André Tanner");
				exit(EXIT_SUCCESS);
			case 'M':
				mouse_events_enabled = !mouse_events_enabled;
				break;
			case 'm': {
				char *mod = argv[++arg];
				if (mod[0] == '^' && mod[1])
					*mod = CTRL(mod[1]);
				for (unsigned int b = 0; b < LENGTH(bindings); b++)
					if (bindings[b].keys[0] == MOD)
						bindings[b].keys[0] = *mod;
				break;
			}
			case 'd':
				set_escdelay(atoi(argv[++arg]));
				if (ESCDELAY < 50)
					set_escdelay(50);
				else if (ESCDELAY > 1000)
					set_escdelay(1000);
				break;
			case 'h':
				screen.history = atoi(argv[++arg]);
				break;
			case 't':
				title = argv[++arg];
				break;
			case 's':
				bar.fd = open_or_create_fifo(argv[++arg], &bar.file);
				updatebarpos();
				break;
			case 'c': {
				const char *fifo;
				cmdfifo.fd = open_or_create_fifo(argv[++arg], &cmdfifo.file);
				if (!(fifo = realpath(argv[arg], NULL)))
					error("%s\n", strerror(errno));
				setenv("DVTM_CMD_FIFO", fifo, 1);
				break;
			}
			case 'e': {
				const char *fifo;
				evtfifo.fd = open_or_create_fifo(argv[++arg], &evtfifo.file);
				if (!(fifo = realpath(argv[arg], NULL)))
					error("%s\n", strerror(errno));
				setenv("DVTM_EVT_FIFO", fifo, 1);
				break;
			}
			case 'y': {
				const char *fifo;
				cpyfifo.fd = __open_or_create_fifo(argv[++arg], &cpyfifo.file, O_RDWR);
				if (!(fifo = realpath(argv[arg], NULL)))
					error("%s\n", strerror(errno));
				setenv("DVTM_CPY_FIFO", fifo, 1);
				break;
			}
			case 'r': {
				const char *fifo;
				retfifo.fd = __open_or_create_fifo(argv[++arg], &retfifo.file, O_RDWR);
				if (!(fifo = realpath(argv[arg], NULL)))
					error("%s\n", strerror(errno));
				setenv("DVTM_RET_FIFO", fifo, 1);
				break;
			}
			case 'b':
				addusrkeyb(argv[++arg]);
				break;
			default:
				usage();
		}
	}
	return init;
}

int
main(int argc, char *argv[]) {
	KeyCombo keys;
	unsigned int key_index = 0;
	memset(keys, 0, sizeof(keys));
	sigset_t emptyset, blockset;

	setenv("DVTM", VERSION, 1);
	if (!parse_args(argc, argv)) {
		setup();
		startup(NULL);
	}

	sigemptyset(&emptyset);
	sigemptyset(&blockset);
	sigaddset(&blockset, SIGWINCH);
	sigaddset(&blockset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &blockset, NULL);

	while (running) {
		int r, nfds = 0;
		fd_set rd;

		if (screen.need_resize) {
			resize_screen();
			screen.need_resize = false;
		}

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);

		if (cmdfifo.fd != -1) {
			FD_SET(cmdfifo.fd, &rd);
			nfds = cmdfifo.fd;
		}

		if (bar.fd != -1) {
			FD_SET(bar.fd, &rd);
			nfds = MAX(nfds, bar.fd);
		}

		for (Client *c = clients; c; ) {
			if (c->overlay && c->overlay_died)
				handle_overlay(c);
			if (!c->overlay && c->died) {
				Client *t = c->next;
				destroy(c);
				c = t;
				continue;
			}
			int pty = c->overlay ? vt_pty_get(c->overlay) : vt_pty_get(c->app);
			FD_SET(pty, &rd);
			nfds = MAX(nfds, pty);
			c = c->next;
		}

		doupdate();
		r = pselect(nfds + 1, &rd, NULL, NULL, NULL, &emptyset);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			int code = getch();
rescan:
			if (code >= 0) {
				keys[key_index++] = code;
				KeyBinding *binding = NULL;
				if (code == KEY_MOUSE) {
					key_index = 0;
					handle_mouse();
				} else if ((binding = keybinding(keys, key_index))) {
					unsigned int key_length = MAX_KEYS;
					int alt_code;

					if (code == ALT) {
						nodelay(stdscr, TRUE);
						alt_code = getch();
						nodelay(stdscr, FALSE);
						if (alt_code >= 0)
							code = alt_code;
						goto rescan;
					}

					while (key_length > 1 && !binding->keys[key_length-1])
						key_length--;
					if (key_index == key_length) {
						binding->action.cmd(binding->action.args);
						key_index = 0;
						memset(keys, 0, sizeof(keys));
					}
				} else {
					key_index = 0;
					memset(keys, 0, sizeof(keys));
					keypress(code);
				}
			}
			if (r == 1) /* no data available on pty's */
				continue;
		}

		if (cmdfifo.fd != -1 && FD_ISSET(cmdfifo.fd, &rd))
			handle_cmdfifo();

		if (bar.fd != -1 && FD_ISSET(bar.fd, &rd))
			handle_statusbar();

		for (Client *c = clients; c; c = c->next) {
			if (FD_ISSET(vt_pty_get(c->term), &rd)) {
				if (vt_process(c->term) < 0 && errno == EIO) {
					if (c->overlay)
						c->overlay_died = true;
					else
						c->died = true;
					continue;
				}
			}

			if (is_content_visible(c)) {
				if (c->sync_title)
					synctitle(c);
				if (c != sel) {
					draw_content(c);
					wnoutrefresh(c->window);
				}
			} else if (!isarrange(fullscreen) && isvisible(c)
					&& c->minimized) {
				draw_border(c);
				wnoutrefresh(c->window);
			}
		}

		if (is_content_visible(sel)) {
			draw_content(sel);
			curs_set(vt_cursor_visible(sel->term));
			wnoutrefresh(sel->window);
		}
	}

	cleanup();
	return 0;
}
