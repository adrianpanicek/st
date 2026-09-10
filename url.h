/* See LICENSE for license details. */

/*
 * Find the URL covering s[pos] in the len bytes at s. It must start with one
 * of the NULL-terminated schemes. On success store its bounds in
 * [*start, *end) and return 1, otherwise return 0.
 */
int urlfind(const char *, int, int, char **, int *, int *);
