# Clickable Hyperlinks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ctrl+click on OSC 8 hyperlinks and plain-text URLs in st opens them with `xdg-open`; Ctrl-hover underlines the link and shows a hand cursor.

**Architecture:** Each cell (`Glyph`) gets a `ushort link` id pointing into a link table in st.c (fills existing struct padding). OSC 8 sets the current id; a mark-and-sweep GC over screen, alt screen, history and saved cursors reclaims ids. Plain URLs are found on demand by a pure scanner in `url.c`. x.c handles click, hover, cursor shape and spawning the opener.

**Tech Stack:** C99, Xlib/Xft, POSIX make. Spec: `docs/superpowers/specs/2026-09-10-hyperlinks-design.md`.

**Commits:** end every commit message with the session trailers:
```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013h8TvCmwhNovDXWovRVs26
```

---

## File map

| File | Change |
|------|--------|
| `url.h`, `url.c` | **new** — `urlfind()`, pure plain-text URL scanner |
| `st.h` | `Glyph.link`, link API decls, `extern char *urlschemes[]` |
| `st.c` | link table, GC, OSC 8 (`case 8`), `linkfind`, hover state, `tclearregion`/`tresize` zeroing, saved cursors to file scope, draw hover refresh, `FD_CLOEXEC` on pty |
| `x.c` | click/release, `openlink`, hover motion, `krelease`, link cursor, underline in `xdrawline` |
| `config.def.h`, `config.h` | `urlschemes`, `urlopener`, `linkmod`, `linkmouseshape` |
| `Makefile` | `url.c` in `SRC`, `test` target |
| `.gitignore` | **new** — build outputs |
| `tests/test.h` | **new** — `CHECK`, `CHECKURI`, `report()` |
| `tests/test_url.c` | **new** — scanner unit tests |
| `tests/test_st.c` | **new** — headless terminal tests (`#include "../st.c"`, stubbed X) |
| `tests/links.sh` | **new** — manual fixtures |

---

### Task 1: URL scanner + test harness

**Files:**
- Create: `url.h`, `url.c`, `tests/test.h`, `tests/test_url.c`, `.gitignore`
- Modify: `Makefile`

- [ ] **Step 1: Write `tests/test.h`**

```c
/* See LICENSE for license details. */
/* minimal test helpers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c) do { if (!(c)) { failures++; \
	fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
	} } while (0)

/* compare a malloc'd string (or NULL) against want, then free it */
#define CHECKURI(got, want) checkuri(__FILE__, __LINE__, (got), (want))

static void
checkuri(const char *file, int line, char *got, const char *want)
{
	if ((got == NULL) != (want == NULL) || (got && strcmp(got, want))) {
		failures++;
		fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", file, line,
		        got ? got : "(null)", want ? want : "(null)");
	}
	free(got);
}

static int
report(void)
{
	fprintf(stderr, "%s\n", failures ? "FAIL" : "ok");
	return failures != 0;
}
```

- [ ] **Step 2: Write `url.h`**

```c
/* See LICENSE for license details. */

/*
 * Find the URL covering s[pos] in the len bytes at s. It must start with one
 * of the NULL-terminated schemes. On success store its bounds in
 * [*start, *end) and return 1, otherwise return 0.
 */
int urlfind(const char *, int, int, char **, int *, int *);
```

- [ ] **Step 3: Write the failing test `tests/test_url.c`**

```c
/* See LICENSE for license details. */
#include <string.h>

#include "../url.h"
#include "test.h"

static char *schemes[] = { "http://", "https://", "mailto:", NULL };

/* 1 if urlfind() on s at pos yields want (NULL: no match) */
static int
found(const char *s, int pos, const char *want)
{
	int b, e;

	if (!urlfind(s, strlen(s), pos, schemes, &b, &e))
		return want == NULL;
	return want && (int)strlen(want) == e - b && !strncmp(s + b, want, e - b);
}

int
main(void)
{
	/* inside, on the scheme, on the last char */
	CHECK(found("see https://example.com/a now", 10, "https://example.com/a"));
	CHECK(found("see https://example.com/a now", 4, "https://example.com/a"));
	CHECK(found("see https://example.com/a now", 24, "https://example.com/a"));
	/* outside the URL */
	CHECK(found("see https://example.com/a now", 1, NULL));
	CHECK(found("see https://example.com/a now", 3, NULL));
	CHECK(found("see https://example.com/a now", 26, NULL));
	/* trailing punctuation is not part of the URL */
	CHECK(found("go to https://x.org.", 8, "https://x.org"));
	CHECK(found("go to https://x.org.", 19, NULL));
	CHECK(found("https://x.org/a?b=1, next", 3, "https://x.org/a?b=1"));
	/* unbalanced closing brackets dropped, balanced kept */
	CHECK(found("(https://x.org/a)", 3, "https://x.org/a"));
	CHECK(found("[https://x.org/a]", 3, "https://x.org/a"));
	CHECK(found("https://en.wikipedia.org/wiki/Foo_(bar)", 3,
	            "https://en.wikipedia.org/wiki/Foo_(bar)"));
	CHECK(found("(https://en.wikipedia.org/wiki/Foo_(bar))", 3,
	            "https://en.wikipedia.org/wiki/Foo_(bar)"));
	/* scheme glued to preceding text */
	CHECK(found("url=https://x.org", 10, "https://x.org"));
	CHECK(found("url=https://x.org", 1, NULL));
	/* other allowed schemes */
	CHECK(found("mail mailto:me@x.org", 8, "mailto:me@x.org"));
	CHECK(found("http://x.org", 0, "http://x.org"));
	/* not allowed or incomplete */
	CHECK(found("javascript:alert(1)", 3, NULL));
	CHECK(found("ftp://x.org", 3, NULL));
	CHECK(found("https://", 3, NULL));
	CHECK(found("https://.", 3, NULL));
	/* non-URL bytes end the token */
	CHECK(found("\"https://x.org\"", 3, "https://x.org"));
	CHECK(found("<https://x.org>", 3, "https://x.org"));
	/* out of range */
	CHECK(found("https://x.org", -1, NULL));
	CHECK(found("https://x.org", 13, NULL));

	return report();
}
```

- [ ] **Step 4: Add the test target to `Makefile`**

Change `SRC = st.c x.c` to `SRC = st.c x.c url.c`; change `st.o: config.h st.h win.h` to `st.o: config.h st.h win.h url.h` and add below it `url.o: url.h`. Add after the `st:` rule:

```make
test: tests/test_url
	./tests/test_url

tests/test_url: tests/test_url.c tests/test.h url.c url.h
	$(CC) $(STCFLAGS) -o $@ tests/test_url.c url.c
```

Change the `clean` recipe to `rm -f st $(OBJ) st-$(VERSION).tar.gz tests/test_url tests/test_st`, add `url.h` after `win.h` in the `dist` `cp` list, and add `test` to `.PHONY`.

- [ ] **Step 5: Run test to verify it fails**

Run: `make test`
Expected: FAIL — `url.c` does not exist (`make: *** No rule to make target 'url.c'`).

- [ ] **Step 6: Write `url.c`**

```c
/* See LICENSE for license details. */
#include <string.h>

#include "url.h"

static int
isurlchar(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') ||
	       (c != '\0' && strchr("-._~:/?#[]@!$&'()*+,;=%", c));
}

/* 1 if closing bracket c has no opening partner in s[b..e) */
static int
unbalanced(const char *s, int b, int e, char c)
{
	char open = (c == ')') ? '(' : '[';
	int depth = 0;

	for (; b < e; b++)
		depth += (s[b] == open) - (s[b] == c);
	return depth < 0;
}

int
urlfind(const char *s, int len, int pos, char **schemes, int *start, int *end)
{
	int b, e, i, n = 0;
	char **sc;

	if (pos < 0 || pos >= len || !isurlchar(s[pos]))
		return 0;
	for (b = pos; b > 0 && isurlchar(s[b - 1]); b--)
		;
	for (e = pos + 1; e < len && isurlchar(s[e]); e++)
		;

	/* the URL starts at the right-most scheme prefix at or before pos */
	for (i = pos; i >= b; i--) {
		for (sc = schemes; *sc; sc++) {
			n = strlen(*sc);
			if (i + n <= e && !strncmp(s + i, *sc, n))
				goto found;
		}
	}
	return 0;

found:
	b = i;
	while (e > b + n) {
		if (strchr(".,;:!?'", s[e - 1]))
			e--;
		else if ((s[e - 1] == ')' || s[e - 1] == ']') &&
		         unbalanced(s, b, e, s[e - 1]))
			e--;
		else
			break;
	}
	if (e <= b + n || pos >= e)
		return 0;
	*start = b;
	*end = e;
	return 1;
}
```

- [ ] **Step 7: Run tests and build**

Run: `make test && make`
Expected: `ok`, then st links without errors.

- [ ] **Step 8: Add `.gitignore`**

```
st
*.o
tests/test_url
tests/test_st
```

- [ ] **Step 9: Commit**

```bash
git add url.c url.h tests/test.h tests/test_url.c Makefile .gitignore
git commit -m "url: add plain-text URL scanner with unit tests"
```

---

### Task 2: Link table and OSC 8 in st.c

**Files:**
- Modify: `st.h:63-68` (Glyph), `st.h` API + externs
- Modify: `st.c` (defines, types, globals, `tcursor`, `tclearregion`, `tresize`, `strhandle`, new link section)
- Modify: `config.def.h`, `config.h`
- Create: `tests/test_st.c`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing test `tests/test_st.c`**

```c
/* See LICENSE for license details. */
/*
 * Headless tests for hyperlink handling. st.c is compiled in directly so its
 * static state is reachable; the X frontend (win.h) is stubbed.
 */
#include "../st.c"
#include "test.h"

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
int xstartdraw(void) { return 0; }
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
}

static void
test_osc8_ids(void)
{
	tnew(80, 24);
	put("\033]8;id=a;https://a.org\033\\ab\033]8;;\033\\ ");
	put("\033]8;id=a;https://a.org\033\\cd\033]8;;\033\\ ");
	put("\033]8;id=b;https://a.org\033\\ef\033]8;;\033\\");
	CHECK(term.line[0][0].link != 0);
	CHECK(term.line[0][0].link == term.line[0][3].link);
	CHECK(term.line[0][0].link != term.line[0][6].link);
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

int
main(void)
{
	test_osc8_basic();
	test_osc8_terminators_and_semicolons();
	test_osc8_attr_lifetime();
	test_osc8_rejected();
	test_osc8_ids();
	test_history_and_gc();
	return report();
}
```

- [ ] **Step 2: Add `tests/test_st` to the Makefile**

```make
test: tests/test_url tests/test_st
	./tests/test_url
	./tests/test_st

tests/test_st: tests/test_st.c tests/test.h st.c st.h win.h url.c url.h
	$(CC) $(STCFLAGS) -o $@ tests/test_st.c url.c $(STLDFLAGS)
```
(replace the existing `test:` rule; keep `tests/test_url:`).

- [ ] **Step 3: Run test to verify it fails**

Run: `make test`
Expected: compile error — `tlinkat` implicitly declared / `LINKMAX` undeclared / `no member named 'link'`.

- [ ] **Step 4: `st.h` — Glyph, API, extern**

```c
typedef struct {
	Rune u;           /* character code */
	ushort mode;      /* attribute flags */
	ushort link;      /* hyperlink id, 0 if none */
	uint32_t fg;      /* foreground  */
	uint32_t bg;      /* background  */
} Glyph;
```
After `char *getsel(void);` add:
```c
char *tlinkat(int, int);
```
After `extern unsigned int defaultcs;` add:
```c
extern char *urlschemes[];
```

- [ ] **Step 5: `config.def.h` and `config.h` — scheme allowlist**

After `static unsigned int mousebg = 0;` in both files add:
```c

/*
 * hyperlinks: only links starting with one of these prefixes are detected
 * (plain text) or accepted (OSC 8)
 */
char *urlschemes[] = { "http://", "https://", "file://", "mailto:", NULL };
```

- [ ] **Step 6: `st.c` — defines, types, globals, prototypes**

After `#include "win.h"` add `#include "url.h"`. After `#define HISTSIZE      2000` add:
```c
#define LINKMAX       4096 /* hyperlink table size, id 0 means no link */
#define LINKURIMAX    8192 /* longest accepted OSC 8 URI */
```
After the `Selection` typedef add:
```c
typedef struct {
	char *uri; /* NULL if the slot is free */
	char *id;  /* OSC 8 id= parameter, NULL if none */
} Link;
```
After `static void strreset(void);` add:
```c
static char *linkfind(int, int, ushort *, int *, int *, int *, int *);
static int linkallowed(const char *);
static void linkgc(void);
static void linkmark(uchar *, Line);
static ushort linknew(const char *, const char *);
static void tsetlink(void);
```
After `static pid_t pid;` add:
```c
static TCursor savedc[2]; /* DECSC cursors: normal and alt screen */
static Link links[LINKMAX];
```

- [ ] **Step 7: `st.c` — saved cursors to file scope (`tcursor`)**

```c
void
tcursor(int mode)
{
	int alt = IS_SET(MODE_ALTSCREEN);

	if (mode == CURSOR_SAVE) {
		savedc[alt] = term.c;
	} else if (mode == CURSOR_LOAD) {
		term.c = savedc[alt];
		tmoveto(savedc[alt].x, savedc[alt].y);
	}
}
```

- [ ] **Step 8: `st.c` — never let blanks inherit a link**

In `tclearregion`, after `gp->mode = 0;` add `gp->link = 0;`.
In `tresize`, in the history padding loop after `term.hist[i][j].u = ' ';` add `term.hist[i][j].link = 0;`.

- [ ] **Step 9: `st.c` — OSC 8 dispatch**

In `strhandle`, inside `case ']':` `switch (par)`, directly before `case 52:` add:
```c
		case 8: /* hyperlink */
			tsetlink();
			return;
```

- [ ] **Step 10: `st.c` — link section**

Insert directly before `void\nstrparse(void)`:
```c
int
linkallowed(const char *uri)
{
	char **sc;

	for (sc = urlschemes; *sc; sc++) {
		if (!strncmp(uri, *sc, strlen(*sc)))
			return 1;
	}
	return 0;
}

void
linkmark(uchar *used, Line line)
{
	int x;

	if (!line)
		return;
	for (x = 0; x < term.col; x++)
		used[line[x].link] = 1;
}

/* free every link no cell or cursor refers to */
void
linkgc(void)
{
	static uchar used[LINKMAX];
	int i;

	memset(used, 0, sizeof(used));
	for (i = 0; i < term.row; i++) {
		linkmark(used, term.line[i]);
		linkmark(used, term.alt[i]);
	}
	for (i = 0; i < HISTSIZE; i++)
		linkmark(used, term.hist[i]);
	used[term.c.attr.link] = 1;
	used[savedc[0].attr.link] = 1;
	used[savedc[1].attr.link] = 1;

	for (i = 1; i < LINKMAX; i++) {
		if (!used[i] && links[i].uri) {
			free(links[i].uri);
			free(links[i].id);
			links[i].uri = links[i].id = NULL;
		}
	}
}

/* id for uri, reusing an entry with the same OSC 8 id; 0 if table is full */
ushort
linknew(const char *uri, const char *id)
{
	static int next;
	int i, tries;

	if (id) {
		for (i = 1; i < LINKMAX; i++) {
			if (links[i].id && !strcmp(links[i].id, id) &&
			    !strcmp(links[i].uri, uri))
				return i;
		}
	}
	for (tries = 0; tries < 2; tries++) {
		for (i = 1; i < LINKMAX; i++) {
			next = next % (LINKMAX - 1) + 1;
			if (!links[next].uri) {
				links[next].uri = xstrdup(uri);
				links[next].id = id ? xstrdup(id) : NULL;
				return next;
			}
		}
		linkgc();
	}
	return 0;
}

/* OSC 8 ; params ; URI -- open a hyperlink, empty URI closes it */
void
tsetlink(void)
{
	char *params, *uri, *id = NULL, *p, *end;

	term.c.attr.link = 0;
	if (strescseq.narg < 3)
		return;
	params = strescseq.args[1];
	uri = strescseq.args[2];

	/* strparse() cut the URI at every ';', glue it back */
	end = strescseq.buf + strescseq.len;
	for (p = uri; p < end; p++) {
		if (*p == '\0')
			*p = ';';
	}
	if (!*uri || strlen(uri) > LINKURIMAX || !linkallowed(uri))
		return;

	/* params are ':'-separated key=value pairs, only id is used */
	for (p = strtok(params, ":"); p; p = strtok(NULL, ":")) {
		if (!strncmp(p, "id=", 3) && p[3])
			id = p + 3;
	}
	term.c.attr.link = linknew(uri, id);
}

/*
 * URI (malloc'd) of the link at screen cell col,row, or NULL. An OSC 8 link
 * stores its id in *id; a plain-text URL stores its cells in *x0,*y0-*x1,*y1.
 */
char *
linkfind(int col, int row, ushort *id, int *x0, int *y0, int *x1, int *y1)
{
	Glyph *gp;

	*id = 0;
	if (!BETWEEN(col, 0, term.col - 1) || !BETWEEN(row, 0, term.row - 1))
		return NULL;

	gp = &TLINE(row)[col];
	if ((gp->mode & ATTR_WDUMMY) && col > 0)
		gp--;
	if (gp->link && links[gp->link].uri) {
		*id = gp->link;
		return xstrdup(links[gp->link].uri);
	}
	return NULL;
}

char *
tlinkat(int col, int row)
{
	ushort id;
	int x0, y0, x1, y1;

	return linkfind(col, row, &id, &x0, &y0, &x1, &y1);
}
```

- [ ] **Step 11: Run tests and build**

Run: `make test && make`
Expected: `ok` twice; st builds.

- [ ] **Step 12: Commit**

```bash
git add st.c st.h config.def.h config.h tests/test_st.c Makefile
git commit -m "st: store OSC 8 hyperlinks per cell with a GC'd link table"
```

---

### Task 3: Plain-text URL lookup and hover state

**Files:**
- Modify: `st.h` (API), `st.c` (`LinkHover`, `hover`, `linkfind`, `linkgc`, hover functions, `draw`)
- Modify: `tests/test_st.c`

- [ ] **Step 1: Add failing tests to `tests/test_st.c`**

Before `main`:
```c
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
```
and call them from `main` after `test_history_and_gc();`:
```c
	test_osc8_hover();
	test_osc8_wins_over_plain();
	test_plain();
	test_plain_wrapped();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: compile error — `tlinkhover`, `tlinkhovered`, `tlinkunhover` undeclared.

- [ ] **Step 3: `st.h` — hover API**

After `char *tlinkat(int, int);` add:
```c
int tlinkhover(int, int);
int tlinkhovered(int, int);
void tlinkunhover(void);
```

- [ ] **Step 4: `st.c` — hover type and state**

After the `Link` typedef add:
```c
typedef struct {
	int active;
	int col, row;       /* pointer cell the hover was computed for */
	ushort link;        /* OSC 8 link id, 0 for a plain-text URL */
	int x0, y0, x1, y1; /* plain-text URL cells, inclusive */
} LinkHover;
```
After `static Link links[LINKMAX];` add `static LinkHover hover;`.
In `linkgc`, after `used[savedc[1].attr.link] = 1;` add `used[hover.link] = 1;`.

- [ ] **Step 5: `st.c` — plain-text path in `linkfind`**

Replace the whole `linkfind` function with:
```c
/*
 * URI (malloc'd) of the link at screen cell col,row, or NULL. An OSC 8 link
 * stores its id in *id; a plain-text URL stores its cells in *x0,*y0-*x1,*y1.
 */
char *
linkfind(int col, int row, ushort *id, int *x0, int *y0, int *x1, int *y1)
{
	Glyph *gp;
	char *buf, *uri = NULL;
	int *cell, x, y, ys, ye, len = 0, pos = -1, b, e;

	*id = 0;
	if (!BETWEEN(col, 0, term.col - 1) || !BETWEEN(row, 0, term.row - 1))
		return NULL;

	gp = &TLINE(row)[col];
	if ((gp->mode & ATTR_WDUMMY) && col > 0)
		gp--;
	if (gp->link && links[gp->link].uri) {
		*id = gp->link;
		return xstrdup(links[gp->link].uri);
	}

	/* plain text: scan the logical line of rows joined by ATTR_WRAP */
	for (ys = row; ys > 0 && (TLINE(ys - 1)[term.col - 1].mode & ATTR_WRAP); ys--)
		;
	for (ye = row; ye < term.row - 1 && (TLINE(ye)[term.col - 1].mode & ATTR_WRAP); ye++)
		;
	buf = xmalloc((ye - ys + 1) * term.col);
	cell = xmalloc((ye - ys + 1) * term.col * sizeof(*cell));
	for (y = ys; y <= ye; y++) {
		for (x = 0; x < term.col; x++) {
			gp = &TLINE(y)[x];
			if (gp->mode & ATTR_WDUMMY)
				continue;
			if (y == row && x == col)
				pos = len;
			cell[len] = y * term.col + x;
			buf[len++] = (gp->u > ' ' && gp->u < 0x7f) ? gp->u : ' ';
		}
	}
	if (pos >= 0 && urlfind(buf, len, pos, urlschemes, &b, &e)) {
		uri = xmalloc(e - b + 1);
		memcpy(uri, buf + b, e - b);
		uri[e - b] = '\0';
		*x0 = cell[b] % term.col;
		*y0 = cell[b] / term.col;
		*x1 = cell[e - 1] % term.col;
		*y1 = cell[e - 1] / term.col;
	}
	free(buf);
	free(cell);
	return uri;
}
```

- [ ] **Step 6: `st.c` — hover functions**

After `tlinkat` add:
```c
/* hover the link under col,row; 1 if there is one */
int
tlinkhover(int col, int row)
{
	LinkHover old = hover;
	char *uri;

	uri = linkfind(col, row, &hover.link, &hover.x0, &hover.y0,
	               &hover.x1, &hover.y1);
	hover.active = uri != NULL;
	hover.col = col;
	hover.row = row;
	free(uri);

	if (hover.active != old.active || hover.link != old.link ||
	    (hover.active && !hover.link &&
	     (hover.x0 != old.x0 || hover.y0 != old.y0 ||
	      hover.x1 != old.x1 || hover.y1 != old.y1)))
		tfulldirt();
	return hover.active;
}

int
tlinkhovered(int x, int y)
{
	if (!hover.active)
		return 0;
	if (hover.link)
		return TLINE(y)[x].link == hover.link;
	return (y > hover.y0 || (y == hover.y0 && x >= hover.x0)) &&
	       (y < hover.y1 || (y == hover.y1 && x <= hover.x1));
}

void
tlinkunhover(void)
{
	if (hover.active)
		tfulldirt();
	hover.active = 0;
	hover.link = 0;
}
```

- [ ] **Step 7: `st.c` — keep hover in sync while drawing**

In `draw()`, directly after
```c
	if (!xstartdraw())
		return;
```
add:
```c

	/* keep the hover under a still pointer in sync with new output */
	if (hover.active)
		tlinkhover(hover.col, hover.row);
```

- [ ] **Step 8: Run tests and build**

Run: `make test && make`
Expected: `ok` twice; st builds.

- [ ] **Step 9: Commit**

```bash
git add st.c st.h tests/test_st.c
git commit -m "st: detect plain-text URLs and track link hover"
```

---

### Task 4: Open links with Ctrl+click

**Files:**
- Modify: `config.def.h`, `config.h`, `st.c` (`ttynew`), `x.c` (includes, globals, `bpress`, `brelease`, `openlink`)

- [ ] **Step 1: config — opener and modifier**

In both `config.def.h` and `config.h`, directly after the `urlschemes` line add:
```c
/* command that opens a link, the URI is appended as last argument */
static char *urlopener[] = { "xdg-open", NULL };
/* modifier to hold for hovering and clicking links */
static uint linkmod = ControlMask;
/* mouse cursor over a hovered link */
static unsigned int linkmouseshape = XC_hand2;
```

- [ ] **Step 2: `st.c` — do not leak the pty into spawned programs**

In `ttynew`, after the `-l line` open succeeds (after the `die("open line ...")` block, before `dup2(cmdfd, 0);`) add:
```c
		fcntl(cmdfd, F_SETFD, FD_CLOEXEC);
```
and in the `default:` branch after `cmdfd = m;` add:
```c
		fcntl(cmdfd, F_SETFD, FD_CLOEXEC);
```

- [ ] **Step 3: `x.c` — includes, globals, prototype**

After `#include <unistd.h>` add:
```c
#include <string.h>
#include <sys/wait.h>
```
After `#include <X11/cursorfont.h>` add `#include <X11/Xutil.h>`.
After `static void mousereport(XEvent *);` add `static void openlink(const char *);`.
After `static uint buttons; /* bit field of pressed buttons */` add:
```c
static int linkclick; /* Button1 press was used to open a link */
```

- [ ] **Step 4: `x.c` — `openlink`**

Directly before `void\nbpress(XEvent *e)` add:
```c
void
openlink(const char *uri)
{
	char *argv[LEN(urlopener) + 1];
	pid_t p;
	int i;

	for (i = 0; urlopener[i]; i++)
		argv[i] = urlopener[i];
	argv[i++] = (char *)uri;
	argv[i] = NULL;

	fprintf(stderr, "st: opening %s\n", uri);
	/* double fork: the opener is reparented to init, no zombie */
	switch ((p = fork())) {
	case -1:
		fprintf(stderr, "fork failed: %s\n", strerror(errno));
		return;
	case 0:
		setsid();
		if (fork() == 0) {
			execvp(argv[0], argv);
			fprintf(stderr, "execvp %s failed: %s\n", argv[0],
			        strerror(errno));
		}
		_exit(0);
	}
	waitpid(p, NULL, 0);
}
```

- [ ] **Step 5: `x.c` — click and release**

In `bpress`, add `char *uri;` to the declarations and insert after the `buttons |= ...` statement:
```c
	if (btn == Button1 && (e->xbutton.state & linkmod) == linkmod &&
	    (uri = tlinkat(evcol(e), evrow(e)))) {
		openlink(uri);
		free(uri);
		linkclick = 1;
		return;
	}
```
In `brelease`, insert after the `buttons &= ...` statement:
```c
	if (btn == Button1 && linkclick) {
		linkclick = 0;
		return;
	}
```

- [ ] **Step 6: Build and run unit tests**

Run: `make && make test`
Expected: builds without errors, `ok` twice.

- [ ] **Step 7: Manual check**

Run: `./st -e sh -c 'printf "\033]8;;https://example.com/osc8\033\\\\osc8 link\033]8;;\033\\\\  https://example.com/plain\n"; exec sh'`
Ctrl+click `osc8 link` → browser opens `https://example.com/osc8`, stderr shows `st: opening https://example.com/osc8`. Ctrl+click the plain URL → opens it. Plain click-drag still selects. `ps -o stat,cmd --ppid $(pgrep -n st)` shows no `Z` entries.

- [ ] **Step 8: Commit**

```bash
git add config.def.h config.h st.c x.c
git commit -m "x: open hyperlinks with Ctrl+click via xdg-open"
```

---

### Task 5: Ctrl-hover underline and hand cursor

**Files:**
- Modify: `x.c` (`XWindow`, prototypes, handler table, `evcol`/`evrow`, `bmotion`, `xinit`, `xsetpointermotion`, `xdrawline`, `focus`, `kpress`, new `krelease`, `xlinkhover`, `xlinkupdate`, `xupdatemotion`)

- [ ] **Step 1: `x.c` — state, prototypes, handler**

In `XWindow`, after `int gm; /* geometry mask */` add:
```c
	Cursor cursor, linkcursor; /* normal and link-hover pointer */
```
After `static int evrow(XEvent *);` add:
```c
static int pxcol(int);
static int pxrow(int);
```
After `static void kpress(XEvent *);` add `static void krelease(XEvent *);`.
After `static void openlink(const char *);` add:
```c
static void xlinkhover(int, int, int);
static void xlinkupdate(void);
static void xupdatemotion(void);
```
In `handler[]`, after `[KeyPress] = kpress,` add `[KeyRelease] = krelease,`.
After `static int linkclick; ...` add:
```c
static int linkmotion;   /* motion events selected for link hover */
static int appmotion;    /* application asked for all motion events */
static int linkhovering; /* link pointer shape is shown */
```

- [ ] **Step 2: `x.c` — pixel to cell helpers**

Replace `evcol` and `evrow` with:
```c
int
pxcol(int x)
{
	x -= borderpx;
	LIMIT(x, 0, win.tw - 1);
	return x / win.cw;
}

int
pxrow(int y)
{
	y -= borderpx;
	LIMIT(y, 0, win.th - 1);
	return y / win.ch;
}

int
evcol(XEvent *e)
{
	return pxcol(e->xbutton.x);
}

int
evrow(XEvent *e)
{
	return pxrow(e->xbutton.y);
}
```

- [ ] **Step 3: `x.c` — hover machinery**

Directly before `void\nbpress(XEvent *e)` add:
```c
/* show (on) or hide the link hover for pointer position px,py */
void
xlinkhover(int on, int px, int py)
{
	int hovering = 0;

	if (on != linkmotion) {
		linkmotion = on;
		xupdatemotion();
	}
	if (on)
		hovering = tlinkhover(pxcol(px), pxrow(py));
	else
		tlinkunhover();
	if (hovering != linkhovering) {
		linkhovering = hovering;
		XDefineCursor(xw.dpy, xw.win,
		              hovering ? xw.linkcursor : xw.cursor);
	}
}

/* re-evaluate the link hover after a modifier key changed */
void
xlinkupdate(void)
{
	Window root, child;
	int rx, ry, x, y;
	uint state;

	if (!XQueryPointer(xw.dpy, xw.win, &root, &child, &rx, &ry, &x, &y,
	                   &state))
		return;
	xlinkhover((state & linkmod) == linkmod &&
	           BETWEEN(x, 0, win.w - 1) && BETWEEN(y, 0, win.h - 1), x, y);
}
```

- [ ] **Step 4: `x.c` — motion**

Replace `bmotion` with:
```c
void
bmotion(XEvent *e)
{
	int linkheld = (e->xmotion.state & linkmod) == linkmod;

	if (linkmotion || linkheld)
		xlinkhover(linkheld, e->xmotion.x, e->xmotion.y);

	if (IS_SET(MODE_MOUSE) && !(e->xbutton.state & forcemousemod)) {
		mousereport(e);
		return;
	}

	mousesel(e, 0);
}
```
Replace `xsetpointermotion` with:
```c
void
xsetpointermotion(int set)
{
	appmotion = set;
	xupdatemotion();
}

void
xupdatemotion(void)
{
	MODBIT(xw.attrs.event_mask, appmotion || linkmotion, PointerMotionMask);
	XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask, &xw.attrs);
}
```

- [ ] **Step 5: `x.c` — cursors in `xinit`**

Delete the local `Cursor cursor;` declaration. Replace
```c
	/* white cursor, black outline */
	cursor = XCreateFontCursor(xw.dpy, mouseshape);
	XDefineCursor(xw.dpy, xw.win, cursor);
```
with
```c
	/* white cursor, black outline */
	xw.cursor = XCreateFontCursor(xw.dpy, mouseshape);
	xw.linkcursor = XCreateFontCursor(xw.dpy, linkmouseshape);
	XDefineCursor(xw.dpy, xw.win, xw.cursor);
```
and replace `XRecolorCursor(xw.dpy, cursor, &xmousefg, &xmousebg);` with
```c
	XRecolorCursor(xw.dpy, xw.cursor, &xmousefg, &xmousebg);
	XRecolorCursor(xw.dpy, xw.linkcursor, &xmousefg, &xmousebg);
```

- [ ] **Step 6: `x.c` — underline hovered cells**

In `xdrawline`, after
```c
		if (selected(x, y1))
			new.mode ^= ATTR_REVERSE;
```
add
```c
		if (tlinkhovered(x, y1))
			new.mode |= ATTR_UNDERLINE;
```

- [ ] **Step 7: `x.c` — modifier keys and focus**

In `kpress`, after the `XmbLookupString`/`XLookupString` if/else block add:
```c
	if (IsModifierKey(ksym))
		xlinkupdate();
```
After `kpress` add:
```c
void
krelease(XEvent *ev)
{
	if (IsModifierKey(XLookupKeysym(&ev->xkey, 0)))
		xlinkupdate();
}
```
In `focus`, in the `else` (FocusOut) branch after `win.mode &= ~MODE_FOCUSED;` add:
```c
		xlinkhover(0, 0, 0);
```

- [ ] **Step 8: Build and run unit tests**

Run: `make && make test`
Expected: builds without errors, `ok` twice.

- [ ] **Step 9: Manual check**

Run the command from Task 4 Step 7. Hold Ctrl over `osc8 link` → underlined, hand cursor; move off → normal. Hold Ctrl over the plain URL → only the URL is underlined. Press Ctrl while the pointer is already on a link (no motion) → underline appears; release → it disappears. Without Ctrl, moving the mouse does not redraw (no underline). In `htop` (mouse mode on), plain clicks still reach htop.

- [ ] **Step 10: Commit**

```bash
git add x.c
git commit -m "x: underline links and show hand cursor on Ctrl-hover"
```

---

### Task 6: Manual fixtures and final verification

**Files:**
- Create: `tests/links.sh`

- [ ] **Step 1: Write `tests/links.sh`**

```sh
#!/bin/sh
# Hyperlink fixtures: run inside a freshly built st, Ctrl+click each link.
osc8() { printf '\033]8;%s;%s\033\\%s\033]8;;\033\\' "$1" "$2" "$3"; }

echo "1. OSC 8, text differs from target:"
printf '   '; osc8 "" "https://example.com/osc8" "click me"; echo
echo "2. Two parts sharing id=x (Ctrl-hover highlights both):"
printf '   '; osc8 "id=x" "https://example.com/shared" "part one"
printf ' and '; osc8 "id=x" "https://example.com/shared" "part two"; echo
echo "3. OSC 8 URI containing ';':"
printf '   '; osc8 "" "https://example.com/a;b;c" "semicolons"; echo
echo "4. Plain URLs:"
echo "   https://example.com/plain"
echo "   trailing period: https://example.com/period."
echo "   parentheses: (https://example.com/paren)"
echo "   wikipedia: https://en.wikipedia.org/wiki/Foo_(bar)"
echo "5. Wrapped plain URL:"
printf '   https://example.com/'
i=0; while [ $i -lt 30 ]; do printf 'long/'; i=$((i + 1)); done; echo end
echo "6. Disallowed scheme, must NOT open:"
printf '   '; osc8 "" "javascript:alert(1)" "evil"; echo
echo "7. Alt screen: tput smcup; sh tests/links.sh; read x; tput rmcup"
```

- [ ] **Step 2: Run fixtures in st**

Run: `./st -e sh -c 'sh tests/links.sh; exec sh'`
Expected: every link 1–5 opens the URL shown in its label (period/paren excluded, Wikipedia paren kept, wrapped URL opens in full ending in `/end`); 6 does nothing; scroll output into history (Shift+PgUp) and links still open; repeat 7 on the alt screen.

- [ ] **Step 3: Final checks**

Run: `make clean && make && make test && git status --short`
Expected: clean build, `ok` twice, only intended files changed.

- [ ] **Step 4: Commit**

```bash
git add tests/links.sh
git commit -m "tests: add manual hyperlink fixtures"
```
