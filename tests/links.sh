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
echo "6. Must NOT open (disallowed scheme, remote file://):"
printf '   '; osc8 "" "javascript:alert(1)" "evil"
printf '  '; osc8 "" "file://otherhost/etc/passwd" "remote file"; echo
echo "7. Local file link:"
printf '   '; osc8 "" "file://$(hostname)$PWD/README" "README"; echo
echo "8. Alt screen: tput smcup; sh tests/links.sh; read x; tput rmcup"
