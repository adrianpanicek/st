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
