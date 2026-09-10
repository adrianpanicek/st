/* See LICENSE for license details. */
#include <string.h>

#include "../url.h"
#include "test.h"

static char *schemes[] = { "http://", "https://", "file://", "mailto:", NULL };

/* 1 if urlfind() on s at pos yields want (NULL: no match) */
static int
found(const char *s, int pos, const char *want)
{
	int b, e;

	if (!urlfind(s, strlen(s), pos, schemes, &b, &e)) {
		if (want)
			fprintf(stderr, "\"%s\" @%d: no match\n", s, pos);
		return want == NULL;
	}
	if (want && (int)strlen(want) == e - b && !strncmp(s + b, want, e - b))
		return 1;
	fprintf(stderr, "\"%s\" @%d: got \"%.*s\"\n", s, pos, e - b, s + b);
	return 0;
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
	/* several schemes in one token: right-most one at or before pos */
	CHECK(found("https://a.com,https://b.com", 3,
	            "https://a.com,https://b.com"));
	CHECK(found("https://a.com,https://b.com", 20, "https://b.com"));
	CHECK(found("https://web.archive.org/web/2020/https://x.org", 10,
	            "https://web.archive.org/web/2020/https://x.org"));
	/* last byte; punctuation after a closing bracket */
	CHECK(found("https://x.org", 12, "https://x.org"));
	CHECK(found("(see https://x.org/a).", 8, "https://x.org/a"));
	/* bytes >= 0x80 are not URL characters */
	CHECK(found("\xe2\x80\x9c" "https://x.org" "\xe2\x80\x9d", 5,
	            "https://x.org"));
	CHECK(found("\xe2\x80\x9c" "https://x.org" "\xe2\x80\x9d", 1, NULL));
	CHECK(found("file:///tmp/x", 3, "file:///tmp/x"));
	/* runs of unmatched closing brackets are trimmed */
	CHECK(found("https://x.org/a)))]]]", 3, "https://x.org/a"));
	/* out of range */
	CHECK(found("https://x.org", -1, NULL));
	CHECK(found("https://x.org", 13, NULL));

	return report();
}
