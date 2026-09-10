/* See LICENSE for license details. */
/*
 * Headless tests for hyperlink handling. st.c is compiled in directly so its
 * static state is reachable; the X frontend (win.h) is stubbed.
 */
#include "../st.c"
#include "test.h"
#include <locale.h>

/* config.h globals normally provided through x.c */
char *utmp = NULL;
char *scroll = NULL;
char *stty_args = "";
char *vtiden = "\033[?6c";
wchar_t *worddelimiters = L" ";
int allowaltscreen = 1;
int allowwindowops = 0;
char *termname = "st-256color";
unsigned int tabspaces = 8;
unsigned int defaultfg = 258;
unsigned int defaultbg = 259;
unsigned int defaultcs = 256;
char *urlschemes[] = { "http://", "https://", "file://", "mailto:", NULL };

/* win.h stubs */
void xbell(void) {}
void xclipcopy(void) {}
void xdrawcursor(int a, int b, Glyph g, int c, int d, Glyph h) {}
void xdrawline(Line l, int x1, int y, int x2) {}
void xfinishdraw(void) {}
void xloadcols(void) {}
int xsetcolorname(int i, const char *n) { return 0; }
int xgetcolor(int i, unsigned char *r, unsigned char *g, unsigned char *b) { return 0; }
void xseticontitle(char *s) {}
void xsettitle(char *s) {}
int xsetcursor(int c) { return 0; }
void xsetmode(int s, unsigned int f) {}
void xsetpointermotion(int s) {}
void xsetsel(char *s) {}
static int drawing; /* let draw() run past xstartdraw() */
int xstartdraw(void) { return drawing; }
void xximspot(int x, int y) {}

static void
put(const char *s)
{
	twrite(s, strlen(s), 0);
}

static void
test_osc8_basic(void)
{
	tnew(80, 24);
	put("\033]8;;https://example.com\033\\click\033]8;;\033\\ plain");
	CHECKURI(tlinkat(0, 0), "https://example.com");
	CHECKURI(tlinkat(4, 0), "https://example.com");
	CHECKURI(tlinkat(5, 0), NULL);
	CHECKURI(tlinkat(6, 0), NULL);
	CHECKURI(tlinkat(-1, 0), NULL);
	CHECKURI(tlinkat(80, 0), NULL);
	CHECKURI(tlinkat(0, 24), NULL);
}

static void
test_osc8_terminators_and_semicolons(void)
{
	tnew(80, 24);
	put("\033]8;;https://x.org/bel\aB\033]8;;\a");
	CHECKURI(tlinkat(0, 0), "https://x.org/bel");

	put("\r\n\033]8;;https://x.org/a;b;c\033\\S\033]8;;\033\\");
	CHECKURI(tlinkat(0, 1), "https://x.org/a;b;c");

	/* more ';' than strparse() keeps as separate args */
	put("\r\n\033]8;;https://x.org/1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17"
	    "\033\\M\033]8;;\033\\");
	CHECKURI(tlinkat(0, 2),
	         "https://x.org/1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17");
}

static void
test_osc8_attr_lifetime(void)
{
	tnew(80, 24);
	/* SGR reset does not close a link */
	put("\033]8;;https://x.org\033\\A\033[0mB\033]8;;\033\\");
	CHECKURI(tlinkat(1, 0), "https://x.org");

	/* erasing while a link is open leaves unlinked blanks */
	put("\033]8;;https://x.org\033\\\033[2J\033]8;;\033\\");
	CHECK(term.line[5][10].link == 0);

	/* resize padding never inherits the open link */
	put("\033]8;;https://x.org\033\\");
	tresize(100, 24);
	CHECK(term.line[0][90].link == 0);
	CHECK(term.hist[0][90].link == 0);
	put("\033]8;;\033\\");
}

static void
test_osc8_rejected(void)
{
	char *seq;
	int n;

	tnew(80, 24);
	put("\033]8;;javascript:alert(1)\033\\X\033]8;;\033\\");
	CHECK(term.line[0][0].link == 0);

	seq = xmalloc(LINKURIMAX + 64);
	n = sprintf(seq, "\r\n\033]8;;https://x.org/");
	memset(seq + n, 'a', LINKURIMAX);
	strcpy(seq + n + LINKURIMAX, "\033\\L\033]8;;\033\\");
	put(seq);
	free(seq);
	CHECK(term.line[1][0].link == 0);

	/* control characters and spaces are not part of a URI */
	put("\r\n\033]8;;https://x.org/a b\033\\S\033]8;;\033\\");
	CHECK(term.line[2][0].link == 0);
}

static void
test_osc8_ids(void)
{
	char *seq;
	int n;

	tnew(80, 24);
	put("\033]8;id=a;https://a.org\033\\ab\033]8;;\033\\ ");
	put("\033]8;id=a;https://a.org\033\\cd\033]8;;\033\\ ");
	put("\033]8;id=b;https://a.org\033\\ef\033]8;;\033\\");
	CHECK(term.line[0][0].link != 0);
	CHECK(term.line[0][0].link == term.line[0][3].link);
	CHECK(term.line[0][0].link != term.line[0][6].link);

	/* over-long id is ignored, the link itself still works */
	seq = xmalloc(LINKIDMAX + 64);
	n = sprintf(seq, "\r\n\033]8;id=");
	memset(seq + n, 'i', LINKIDMAX + 1);
	strcpy(seq + n + LINKIDMAX + 1, ";https://x.org\033\\I\033]8;;\033\\");
	put(seq);
	free(seq);
	CHECK(term.line[1][0].link != 0);
	CHECK(links[term.line[1][0].link].id == NULL);
}

static void
test_history_and_gc(void)
{
	char seq[64], want[32];
	int i;

	tnew(80, 24);
	put("\033]8;;https://hist.org\033\\H\033]8;;\033\\");
	for (i = 0; i < 30; i++)
		put("\n");

	/* scrolled into history, still found when scrolled back */
	kscrollup(&(Arg){ .i = 7 });
	CHECKURI(tlinkat(0, 0), "https://hist.org");
	kscrolldown(&(Arg){ .i = 7 });

	/* GC under pressure frees dead links, keeps live ones */
	put("\033[5;1H\033]8;;https://live.org\033\\L\033]8;;\033\\");
	put("\033[H\033]8;;https://saved.org\033\\\0337\033]8;;\033\\");
	for (i = 0; i < 3 * LINKMAX; i++) {
		snprintf(seq, sizeof(seq),
		         "\033]8;;https://x.org/%d\033\\A\033]8;;\033\\\r", i);
		put(seq);
	}
	snprintf(want, sizeof(want), "https://x.org/%d", i - 1);
	CHECKURI(tlinkat(0, 0), want);
	CHECKURI(tlinkat(0, 4), "https://live.org");
	put("\0338Z");
	CHECKURI(tlinkat(0, 0), "https://saved.org");
	kscrollup(&(Arg){ .i = 7 });
	CHECKURI(tlinkat(0, 0), "https://hist.org");
}

static void
test_wide_chars(void)
{
	tnew(80, 24);
	/* the dummy half of an unlinked wide char drops an old link */
	put("\033]8;;https://old.org\033\\AB\033]8;;\033\\\r中\rx");
	CHECK(term.line[0][1].link == 0);
	/* both halves of a linked wide char resolve to its link */
	put("\r\n\033]8;;https://wide.org\033\\中\033]8;;\033\\");
	CHECKURI(tlinkat(0, 1), "https://wide.org");
	CHECKURI(tlinkat(1, 1), "https://wide.org");
	/* overwriting the dummy half leaves an unlinked blank */
	put("\033[2;2Hy");
	CHECK(term.line[1][0].link == 0);
}

static void
test_table_full_evicts_history(void)
{
	char seq[64], want[32];
	int i, n = LINKMAX + 300;

	tnew(80, 24);
	/* three live links per line, more than the table holds */
	for (i = 0; i < n; i++) {
		snprintf(seq, sizeof(seq),
		         "\033]8;;https://x.org/%d\033\\L\033]8;;\033\\%s",
		         i, (i % 3 == 2) ? "\r\n" : " ");
		put(seq);
	}
	/* the newest link still got an id */
	snprintf(want, sizeof(want), "https://x.org/%d", n - 1);
	CHECKURI(tlinkat(((n - 1) % 3) * 2, 23), want);
	/* the oldest history line gave its links up */
	kscrollup(&(Arg){ .i = n / 3 - 23 });
	CHECK(TLINE(0)[0].u == 'L');
	CHECK(TLINE(0)[0].link == 0);
}

static void
test_osc8_hover(void)
{
	tnew(80, 24);
	put("\033]8;id=a;https://a.org\033\\ab\033]8;;\033\\ ");
	put("\033]8;id=a;https://a.org\033\\cd\033]8;;\033\\ ");
	put("\033]8;id=b;https://a.org\033\\ef\033]8;;\033\\");
	CHECK(tlinkhover(0, 0));
	CHECK(tlinkhovered(1, 0));
	CHECK(!tlinkhovered(2, 0));
	CHECK(tlinkhovered(4, 0));
	CHECK(!tlinkhovered(6, 0));
	tlinkunhover();
	CHECK(!tlinkhovered(0, 0));
}

static void
test_osc8_wins_over_plain(void)
{
	tnew(80, 24);
	put("\033]8;;https://a.org\033\\https://b.org\033]8;;\033\\");
	CHECKURI(tlinkat(3, 0), "https://a.org");
}

static void
test_plain(void)
{
	tnew(80, 24);
	put("see https://example.com/a now");
	CHECKURI(tlinkat(10, 0), "https://example.com/a");
	CHECKURI(tlinkat(1, 0), NULL);
	CHECK(!tlinkhover(1, 0));
	CHECK(tlinkhover(10, 0));
	CHECK(tlinkhovered(4, 0));
	CHECK(tlinkhovered(24, 0));
	CHECK(!tlinkhovered(3, 0));
	CHECK(!tlinkhovered(25, 0));
	tlinkunhover();
}

static void
test_plain_wrapped(void)
{
	tnew(20, 5);
	/* row 0: "xx https://example.c", row 1: "om/very/long/path" */
	put("xx https://example.com/very/long/path");
	CHECKURI(tlinkat(5, 0), "https://example.com/very/long/path");
	CHECKURI(tlinkat(3, 1), "https://example.com/very/long/path");
	CHECK(tlinkhover(3, 1));
	CHECK(!tlinkhovered(2, 0));
	CHECK(tlinkhovered(3, 0));
	CHECK(tlinkhovered(19, 0));
	CHECK(tlinkhovered(16, 1));
	CHECK(!tlinkhovered(17, 1));
	tlinkunhover();
}

static void
test_plain_wrapped_scrolled(void)
{
	tnew(20, 5);
	put("xx https://example.com/very/long/path\r\n1\r\n2\r\n3\r\n4\r\n5");
	term.scr = 6;	/* first URL row at the bottom of the view */
	CHECKURI(tlinkat(5, 4), "https://example.com/very/long/path");
	term.scr = 1;	/* second URL row at the top of the view */
	CHECKURI(tlinkat(3, 0), "https://example.com/very/long/path");
	term.scr = 0;
}

static void
test_hover_draw(void)
{
	tnew(80, 5);
	drawing = 1;
	put("see https://example.com/a now");
	CHECK(tlinkhover(10, 0));
	/* the link scrolls away under a still pointer */
	put("\r\n\r\n\r\n\r\n\r\n");
	draw();
	CHECK(!tlinkhovering());
	CHECK(!tlinkhovered(10, 0));
	/* a link appears under the still pointer */
	put("\033[H\033]8;;https://b.org\033\\0123456789ABC\033]8;;\033\\");
	draw();
	CHECK(tlinkhovering());
	CHECK(tlinkhovered(10, 0));
	/* once unhovered, drawing does not bring it back */
	tlinkunhover();
	draw();
	CHECK(!tlinkhovering());
	drawing = 0;
}

int
main(void)
{
	CHECK(setlocale(LC_CTYPE, "C.UTF-8") != NULL);
	test_osc8_basic();
	test_osc8_terminators_and_semicolons();
	test_osc8_attr_lifetime();
	test_osc8_rejected();
	test_osc8_ids();
	test_history_and_gc();
	test_wide_chars();
	test_table_full_evicts_history();
	test_osc8_hover();
	test_osc8_wins_over_plain();
	test_plain();
	test_plain_wrapped();
	test_plain_wrapped_scrolled();
	test_hover_draw();
	return report();
}
