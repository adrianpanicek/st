# Clickable hyperlinks in st — design

Date: 2026-09-10
Branch: `feature/hyperlinks`

## Goal

Links printed by programs (notably AI coding agents) become clickable in st and
open in the user's preferred browser. Two link sources are supported:

1. **OSC 8 hyperlinks** — `ESC ] 8 ; params ; URI ST` … text … `ESC ] 8 ; ; ST`.
   Link text may differ from the target URI.
2. **Plain-text URLs** — `https://…` etc. printed as ordinary text.

## User-facing behaviour

- **Ctrl + left click** on a link opens it via `xdg-open`.
- While **Ctrl is held** and the pointer is over a link, the link is
  underlined and the pointer becomes a hand (`XC_hand2`).
- Plain left click, selection, and Ctrl+click on non-link cells behave exactly
  as before.
- Ctrl+click on a link works even when the running application has mouse
  reporting enabled; the press *and* its matching release are consumed by st.
- Only URIs whose scheme is on an allowlist are opened. The opened URI is
  printed to stderr.

## Configuration (`config.def.h` / `config.h`)

```c
/* hyperlinks: command used to open a link; the URI is appended as last arg */
static char *urlopener[] = { "xdg-open", NULL };
/* only links starting with one of these prefixes are detected/opened */
char *urlschemes[] = { "http://", "https://", "file://", "mailto:", NULL };
/* modifier that must be held to hover/click links */
static uint linkmod = ControlMask;
/* mouse cursor shown over a hovered link */
static unsigned int linkmouseshape = XC_hand2;
```

`urlschemes` is non-static and declared `extern` in `st.h` because st.c uses
it (same pattern as `scroll`, `termname`, …).

## Architecture

### Terminal side (st.c / st.h)

**Cell storage.** `Glyph` gains `ushort link` (0 = no link). It occupies
existing struct padding, so `sizeof(Glyph)` stays 16. `ATTRCMP` is unchanged
(links do not affect rendering by themselves).

- `term.c.attr.link` holds the currently open OSC 8 link; `tsetchar` copies it
  into each cell through the existing `*attr` copy.
- `tclearregion` sets `gp->link = 0`, so erased cells never inherit a link.
- `tresize` history padding copies `term.c.attr`; it must also zero `link`.
- SGR 0 does **not** close a link (OSC 8 is independent of SGR). RIS
  (`treset`) closes it because it reinitialises `term.c.attr`.

**Link table.** `static struct { char *uri; char *id; } links[LINKMAX]`,
`LINKMAX = 4096`, index 0 unused.

- OSC 8 with an `id=` param: reuse an existing entry with equal `id` and `uri`.
- Otherwise: allocate a free entry.
- When no entry is free, run **GC**: mark every id referenced by `term.line`,
  `term.alt`, `term.hist`, `term.c.attr` and the saved cursors, free every
  unmarked entry, retry. The saved cursors (`static TCursor c[2]` inside
  `tcursor`) move to file scope so GC can see them. If still full, the link is
  dropped (`attr.link = 0`) — text still prints.
- URIs longer than `LINKURIMAX = 8192` bytes are ignored (link not opened).

**OSC 8 parsing** (`strhandle`, `case 8`). `strparse` splits in place by
overwriting `;` with `\0` and stops after `STR_ARG_SIZ` (16) args, so the URI is
rebuilt by turning every `\0` in `[args[2], buf+len)` back into `;` — this also
recovers URIs with more than 13 semicolons. Params (`args[1]`) are
`:`-separated `key=value`; only `id` is used. Empty URI ⇒ close link.
Malformed (`narg < 3`) ⇒ close link.

**Plain-URL scanner** (on demand only; nothing runs while output streams).
Steps 3–5 are a pure function `urlfind()` in new `url.c`/`url.h` so it can be
unit-tested without a terminal.

1. Build the *logical line* around screen row `y`: walk up while the previous
   row's last cell has `ATTR_WRAP`, walk down while the current row does.
   Only on-screen rows (`TLINE(0..term.row-1)`) are considered.
2. Map each non-`ATTR_WDUMMY` cell to one byte; non-ASCII cells become a
   terminator byte.
3. From the clicked cell, expand left and right over URL-legal ASCII
   (`A-Za-z0-9 -._~:/?#[]@!$&'()*+,;=%`).
4. Inside that token, find the right-most occurrence of any `urlschemes`
   prefix that starts at or before the clicked cell; the URL starts there.
5. Trim trailing `.,;:!?'"`; trim a trailing `)` / `]` only if unbalanced
   within the URL (keeps `…/Foo_(bar)` intact).
6. Clicked cell must lie inside the result, else no link.

**API exported to x.c** (st.h):

```c
char *tlinkat(int col, int row);  /* malloc'd URI under cell or NULL */
int   tlinkhover(int col, int row); /* set hover to link under cell */
void  tlinkunhover(void);
int   tlinkhovered(int col, int row);
```

- `tlinkat`: OSC 8 wins (`TLINE(row)[col].link != 0`), else plain scanner.
  OSC 8 links with a scheme outside `urlschemes` are never stored; the plain
  scanner only matches `urlschemes` prefixes.
- `tlinkhover` returns 1 when a link is under the cell (x.c uses it for the
  pointer shape).
- Hover state is either an OSC 8 id (hovered = every visible cell with that
  id) or a plain-URL range `(x0,y0)-(x1,y1)` in screen coordinates. Changing
  hover marks the old and new rows dirty.
- `draw()` re-evaluates an active hover at its stored pointer cell before
  drawing, so the underline follows content that scrolls under a still
  pointer.

### X side (x.c)

- **Click.** In `bpress`, before `mousereport`/`mouseaction`: if
  `btn == Button1` and `(state & linkmod) == linkmod` and `tlinkat()` returns a
  URI ⇒ `openlink(uri)`, set `linkclick = 1`, return. `brelease` swallows the
  matching Button1 release while `linkclick` is set.
- **Hover.** `PointerMotionMask` is selected only while `linkmod` is held
  (or the application asked for mode 1003): `xsetpointermotion` records the
  application's wish, `xupdatemotion` ORs it with the link-hover state. This
  avoids a full-window redraw on every mouse move otherwise. In `bmotion`,
  hover iff `(state & linkmod) == linkmod`; `mousereport`/`selextend` already
  ignore button-less motion. On KeyPress/KeyRelease of a
  modifier key (`IsModifierKey`), `XQueryPointer` gives current mask and
  position; hover or unhover accordingly. A KeyRelease handler is added
  (`KeyReleaseMask` is already selected). `FocusOut` unhovers.
- **Pointer shape.** `xinit` creates both the normal and the link cursor;
  hover state changes call `XDefineCursor`.
- **Underline.** `xdrawline`: `if (tlinkhovered(x, y1)) new.mode |= ATTR_UNDERLINE;`
- **Opening.** `openlink(const char *uri)`: double `fork` (grandchild is
  reparented to init, so st's shell-only `sigchld` never sees it), `setsid`,
  `execvp(urlopener[0], urlopener + uri)`; no shell, so no injection. The
  scheme allowlist also blocks URIs beginning with `-`.
- **fd hygiene.** `ttynew` sets `FD_CLOEXEC` on `cmdfd` (both the `openpty`
  and the `-l line` paths) so the browser does not hold the pty.

## Error handling

- Link table exhaustion ⇒ GC, then silent drop of the link.
- `fork`/`exec` failure ⇒ message to stderr, st keeps running.
- OSC 8 with a disallowed scheme ⇒ link not stored; its text stays plain text.

## Security notes

- OSC 8 text can lie about its target; st has no status bar, so the real URI
  is only printed to stderr. Accepted risk, same as other terminals without a
  URL preview.
- Scheme allowlist prevents `javascript:`, custom protocol handlers, and
  option injection into the opener.

## Testing

No test suite exists. Add `make test` with two headless C test programs:
`tests/test_url` (scanner) and `tests/test_st` (compiles `st.c` directly with
stubbed `win.h` functions, drives `twrite()` with escape sequences and checks
`tlinkat`/hover results, including GC under pressure).

Also add `tests/links.sh`, a script that prints fixtures for manual checks:

1. OSC 8 link, link text ≠ URI.
2. Two OSC 8 links sharing `id=`; hover highlights both.
3. OSC 8 URI containing `;`.
4. Plain `https://` URL; URL with trailing `.`; URL in parentheses;
   Wikipedia URL with `(…)` in the path.
5. Plain URL long enough to wrap over two rows.
6. OSC 8 link scrolled into history, then scrolled back into view.
7. Link on the alternate screen (`tput smcup`).
8. (unit test, not a fixture) 3×`LINKMAX` distinct OSC 8 links force GC
   repeatedly; live links on screen, in history and in a saved cursor survive.
9. Disallowed scheme (`javascript:alert(1)`) — must not open.

Manual: Claude Code session inside st, Ctrl+click its links.

## Out of scope

- Keyboard hint mode.
- Showing the target URI in the window (status bar / title).
- Detecting URLs in history rows that are not currently on screen.
