#!/usr/bin/env bash
#
# style-selftest.sh -- prove style-check.sh rejects each kind of violation.
# Starts from this checkout (which must conform) and mutates a copy.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
check="$here/tools/style-check.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fails=0

fresh() {
	rm -rf "$tmp/r"
	mkdir -p "$tmp/r/docs"
	cp "$here/README.md" "$tmp/r/"
	cp "$here/docs/logo.svg" "$tmp/r/docs/"
}
expect() { # expect pass|fail <name> <python-edit or empty>
	fresh
	[ -n "$3" ] && (cd "$tmp/r" && python3 -c "$3")
	"$check" "$tmp/r" >"$tmp/out" 2>&1
	rc=$?
	if { [ "$1" = pass ] && [ $rc -eq 0 ]; } || { [ "$1" = fail ] && [ $rc -ne 0 ]; }; then
		echo "ok   $1  $2"
	else
		echo "FAIL expected $1, rc=$rc: $2"
		sed 's/^/       /' "$tmp/out"
		fails=$((fails + 1))
	fi
}
readme() { echo "p='README.md';s=open(p).read();$1;open(p,'w').write(s)"; }
logo() { echo "p='docs/logo.svg';s=open(p).read();$1;open(p,'w').write(s)"; }

expect pass "this repository conforms" ""
expect fail "logo path wrong" "$(readme "s=s.replace('docs/logo.svg','logo.svg',1)")"
expect fail "logo width wrong" "$(readme "s=s.replace('width=\"420\"','width=\"300\"',1)")"
expect fail "no CI badge" "$(readme "s=s.replace('actions/workflows/ci.yml/badge.svg','x',1)")"
expect fail "release badge without sort=semver" "$(readme "s=s.replace('?sort=semver','',1)")"
expect fail "no sponsors badge" "$(readme "s=s.replace('github/sponsors','github/x',1)")"
expect fail "licence badge without label" "$(readme "s=s.replace('?label=licence','',1)")"
expect fail "badges out of order" "$(readme "s=s.replace('badge.svg\" alt=\"CI\"></a>','badge.svg\" alt=\"CI\"></a>XX',1).replace('github/sponsors','ZZ',1)")"
expect fail "heading differs from logo alt" "$(readme "s=s.replace('# Wolfram\n','# Wolfram SDK\n',1)")"
expect fail "stray version badge" "$(readme "s+='\n![version](https://img.shields.io/github/v/release/x/y?label=version)\n'")"
expect fail "contributing after licence" "$(readme "a=s.index('## Contributing');b=s.index('## Support');c=s[a:b];s=s[:a]+s[b:];s=s.replace('## License',c+'## License',1);s=s.replace(c+'## License','## License',1)+c.replace('## Contributing','## Contributing')")"
expect fail "no contributing section" "$(readme "s=s.replace('## Contributing','## Thanks',1)")"
expect fail "logo viewBox wrong" "$(logo "s=s.replace('viewBox=\"0 0 294','viewBox=\"0 0 300',1)")"
expect fail "logo not crisp" "$(logo "s=s.replace('shape-rendering=\"crispEdges\"','',1)")"
expect fail "logo without role" "$(logo "s=s.replace('role=\"img\"','',1)")"
expect fail "logo wrong dark colour" "$(logo "s=s.replace('#4ade80','#ffffff')")"
expect fail "logo has a path" "$(logo "s=s.replace('</g>','<path d=\"M0 0\"/></g>',1)")"
expect fail "logo rect off the grid" "$(logo "s=s.replace('<rect x=\"264\"','<rect x=\"265\"',1)")"
expect fail "logo sets a fill attribute" "$(logo "s=s.replace('<rect x=\"264\"','<rect fill=\"red\" x=\"264\"',1)")"

echo
if [ $fails -ne 0 ]; then
	echo "style-selftest: $fails case(s) behaved wrongly"
	exit 1
fi
echo "style-selftest: all cases behaved"
