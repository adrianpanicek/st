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

int
urlfind(const char *s, int len, int pos, char **schemes, int *start, int *end)
{
	int b, e, i, n = 0, paren, bracket;
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
	/* bracket depth of the URL, below 0 means unmatched closing brackets */
	paren = bracket = 0;
	for (i = b; i < e; i++) {
		paren += (s[i] == '(') - (s[i] == ')');
		bracket += (s[i] == '[') - (s[i] == ']');
	}
	while (e > b + n) {
		if (strchr(".,;:!?'", s[e - 1]))
			e--;
		else if (s[e - 1] == ')' && paren < 0)
			e--, paren++;
		else if (s[e - 1] == ']' && bracket < 0)
			e--, bracket++;
		else
			break;
	}
	if (e <= b + n || pos >= e)
		return 0;
	*start = b;
	*end = e;
	return 1;
}
