#!/bin/bash
# e2e_bridge.sh - Kiem tra dau cuoi: client C -> (khung bridge) -> server Go -> usecase -> Postgres
#
# Dieu kien truoc khi chay:
#   1. Postgres da nap backend/db/full_setup.sql (co du lieu mau, chuyen bay bat dau tu ngay mai)
#   2. Server Go dang chay:  cd backend && go run ./cmd/server   (BRIDGE_ADDRESS mac dinh :9100)
#   3. Da bien dich:         make   (trong thu muc netcore)
#
# Chay:  ./tests/e2e_bridge.sh [ip] [port]
# Moi lenh gui di duoc doi chieu voi dong "<< KIND TYPE status=N" mong doi, theo dung thu tu.

cd "$(dirname "$0")/.." || exit 1
IP="${1:-127.0.0.1}"
PORT="${2:-9100}"
CLI=./bin/bridge_cli
[ -x "$CLI" ] || { echo "Chua co $CLI, hay chay make truoc"; exit 1; }

TOMORROW=$(date -d tomorrow +%F)
STAMP=$(date +%s)
EMAIL="e2e.$STAMP@example.com"

# Moi phan tu: "lenh gui|||dau dong phan hoi mong doi"
CASES=(
  'health|||<< RESPONSE SYS_HEALTH status=0'
  'list 1 3|||<< RESPONSE FLIGHT_LIST status=0'
  'flight 1|||<< RESPONSE FLIGHT_GET status=0'
  'flight 99999999|||<< RESPONSE FLIGHT_GET status=404'
  'flight 0|||<< RESPONSE FLIGHT_GET status=400'
  "search \"Ha Noi\" \"Ho Chi Minh City\" $TOMORROW|||<< RESPONSE FLIGHT_SEARCH status=0"
  'search "Ha Noi" "Ho Chi Minh City" 2099-01-01|||<< RESPONSE FLIGHT_SEARCH status=404'
  'search "Ha Noi" "Ho Chi Minh City" 31-12-2026|||<< RESPONSE FLIGHT_SEARCH status=400'
  'logout|||<< RESPONSE AUTH_LOGOUT status=401'
  'login john.doe@example.com sai-mat-khau|||<< RESPONSE AUTH_LOGIN status=401'
  'login john.doe@example.com 123456|||<< RESPONSE AUTH_LOGIN status=0'
  'logout|||<< RESPONSE AUTH_LOGOUT status=0'
  "register Nguyen Van $EMAIL 123456|||<< RESPONSE AUTH_REGISTER status=0"
  "register Nguyen Van $EMAIL 123456|||<< RESPONSE AUTH_REGISTER status=409"
  'register A B email-sai 123456|||<< RESPONSE AUTH_REGISTER status=400'
  'register A B ok@example.com 123|||<< RESPONSE AUTH_REGISTER status=400'
  "login $EMAIL 123456|||<< RESPONSE AUTH_LOGIN status=0"
  'raw 0x7FFF {}|||<< RESPONSE ? status=501'
  'raw 0x0102 not-json|||<< RESPONSE AUTH_LOGIN status=400'
  'slow 0x0102 {"email":"admin@qairlines.vn","password":"123456"}|||<< RESPONSE AUTH_LOGIN status=0'
  'ping|||<< PONG'
)

SCRIPT=""
EXPECT=()
for c in "${CASES[@]}"; do
  SCRIPT+="${c%%|||*}"$'\n'
  EXPECT+=("${c##*|||}")
done
SCRIPT+=$'quit\n'

# Chi giu cac dong tom tat phan hoi ("<< ..."), bo qua dong payload JSON
mapfile -t GOT < <(printf '%s' "$SCRIPT" | "$CLI" -q "$IP" "$PORT" | grep '^<< ')

fail=0
if [ "${#GOT[@]}" -ne "${#EXPECT[@]}" ]; then
  echo "FAIL: nhan ${#GOT[@]} phan hoi, mong doi ${#EXPECT[@]}"
  fail=1
fi
for i in "${!EXPECT[@]}"; do
  line="${GOT[$i]:-<khong co>}"
  cmd="${CASES[$i]%%|||*}"
  if [[ "$line" == "${EXPECT[$i]}"* ]]; then
    printf 'ok    %-70s -> %s\n' "$cmd" "${line:3}"
  else
    printf 'FAIL  %-70s\n        mong doi: %s\n        nhan duoc: %s\n' "$cmd" "${EXPECT[$i]}" "$line"
    fail=1
  fi
done

# Tim kiem ngay mai phai tra ve nhieu hang (du lieu mau co 4 hang bay tuyen HAN-SGN)
RESULT=$(printf 'search "Ha Noi" "Ho Chi Minh City" %s\nquit\n' "$TOMORROW" | "$CLI" -q "$IP" "$PORT")
airlines=$(printf '%s' "$RESULT" | grep -o '"airline":"[^"]*"' | sort -u | wc -l)
if [ "$airlines" -ge 3 ]; then
  echo "ok    tim kiem ngay mai tra ve $airlines hang bay khac nhau"
else
  echo "FAIL  tim kiem ngay mai chi co $airlines hang bay"
  fail=1
fi

[ "$fail" -eq 0 ] && echo "TAT CA DAT" || echo "CO LOI"
exit "$fail"
