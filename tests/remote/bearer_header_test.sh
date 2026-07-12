#!/usr/bin/env bash
# V5e: the head sends `Authorization: Bearer <token>` on its GraphQL
# requests when a token is configured (env or a matching backend entry
# from Preferences > Connection).  A capture server asserts the header
# byte-exactly in the form the pylon's auth gate compares against.
set -u
BUILD="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
QTAMP="$BUILD/qtamp"
[ -x "$QTAMP" ] || { echo "no qtamp at $QTAMP"; exit 2; }

tmp="$(mktemp -d)"
trap 'kill $NPID 2>/dev/null; rm -rf "$tmp"' EXIT

node -e '
const http = require("http");
const srv = http.createServer((req, res) => {
  if (req.url.startsWith("/graphql"))
    console.log("AUTH:" + (req.headers.authorization ?? "(none)"));
  res.writeHead(200, {"content-type": "application/json"});
  res.end(JSON.stringify({data: null}));
});
srv.listen(0, "127.0.0.1", () => console.log("PORT:" + srv.address().port));
setTimeout(() => process.exit(0), 40000);
' > "$tmp/srv.log" &
NPID=$!
for _ in $(seq 1 50); do grep -q '^PORT:' "$tmp/srv.log" && break; sleep 0.1; done
PORT=$(sed -n 's/^PORT://p' "$tmp/srv.log" | head -1)
[ -n "$PORT" ] || { echo "capture server never came up"; exit 1; }

# Leg 1: env token.
QT_QPA_PLATFORM=offscreen QTAMP_BEARER_TOKEN=sekrit-env timeout 6 \
  "$QTAMP" --connect "graphql+http://127.0.0.1:$PORT" --probe playing \
  >/dev/null 2>&1
# Leg 2: token from a stored backend entry matching the URL.
H="$tmp/home"; mkdir -p "$H/.config/winamp"
cat > "$H/.config/winamp/winamp.conf" <<EOC
[backends]
1\name=Capture
1\url=graphql+http://127.0.0.1:$PORT
1\token=sekrit-conf
size=1
EOC
HOME="$H" QT_QPA_PLATFORM=offscreen timeout 6 \
  "$QTAMP" --connect "graphql+http://127.0.0.1:$PORT" --probe playing \
  >/dev/null 2>&1

# Leg 3: canonicalized matching — stored URL has graphql+ prefix and a
# trailing slash, --connect uses the bare form.
cat > "$H/.config/winamp/winamp.conf" <<EOC
[backends]
1\name=Canon
1\url=graphql+http://127.0.0.1:$PORT/
1\token=sekrit-canon
size=1
EOC
HOME="$H" QT_QPA_PLATFORM=offscreen timeout 6 \
  "$QTAMP" --connect "http://127.0.0.1:$PORT" --probe playing \
  >/dev/null 2>&1
# Leg 4: QTAMP_BEARER_TOKEN set-but-empty = explicit no-auth even with
# a matching stored token.
HOME="$H" QT_QPA_PLATFORM=offscreen QTAMP_BEARER_TOKEN= timeout 6 \
  "$QTAMP" --connect "http://127.0.0.1:$PORT" --probe playing \
  >/dev/null 2>&1
# Leg 5: startup resolution — no --connect at all; the persisted active
# backend supplies URL + token.
cat > "$H/.config/winamp/winamp.conf" <<EOC
[connection]
active=Stored
[backends]
1\name=Stored
1\url=graphql+http://127.0.0.1:$PORT
1\token=sekrit-active
size=1
EOC
HOME="$H" QT_QPA_PLATFORM=offscreen timeout 6 \
  "$QTAMP" --probe playing >/dev/null 2>&1

fail=0
grep -q '^AUTH:Bearer sekrit-env$' "$tmp/srv.log" \
  && echo "  OK     env token sent as exact Bearer header" \
  || { echo "  FAIL   env token"; fail=1; }
grep -q '^AUTH:Bearer sekrit-conf$' "$tmp/srv.log" \
  && echo "  OK     stored backend token sent" \
  || { echo "  FAIL   stored token"; fail=1; }
grep -q '^AUTH:Bearer sekrit-canon$' "$tmp/srv.log" \
  && echo "  OK     canonicalized URL match (prefix + trailing slash)" \
  || { echo "  FAIL   canonicalized match"; fail=1; }
grep -q '^AUTH:(none)$' "$tmp/srv.log" \
  && echo "  OK     empty env token = explicit no-auth" \
  || { echo "  FAIL   empty-env override"; fail=1; }
grep -q '^AUTH:Bearer sekrit-active$' "$tmp/srv.log" \
  && echo "  OK     persisted active backend resolves at startup" \
  || { echo "  FAIL   startup resolution"; fail=1; }
[ $fail = 0 ] && echo "bearer_header_test: all checks passed"
exit $fail
