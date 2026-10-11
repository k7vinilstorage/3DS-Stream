#!/bin/sh
# A parte de rede do probe contra o servidor PSPStream de verdade:
#   ./run_net.sh <checkout do PSP-Stream> [python]
# O servidor roda com --source test --size 400x240 (precisa do GStreamer, como no CI do PSP-Stream).
set -e
cd "$(dirname "$0")"
PSP=${1:?usage: run_net.sh <PSP-Stream checkout> [python]}
PY=${2:-python3}
make -s probe_host
# sem resposta ao broadcast, o probe usa server.txt
mkdir -p "sdmc:/3ds/3ds-stream" && echo "server=127.0.0.1" > "sdmc:/3ds/3ds-stream/server.txt"
$PY "$PSP/server/pspstream.py" --source test --size 400x240 --no-web > server.out 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
for i in $(seq 30); do grep -q "waiting for the PSP" server.out && break; sleep 0.5; done
./probe_host net | tee net.out
grep -q "discovery: server at\|server: 127.0.0.1" net.out
grep -q "ping: 20/20" net.out
# as duas fases do stream: frames chegando a 400x240
test "$(grep -c "fps, [1-9][0-9]* frames" net.out)" -eq 2
! grep -q "NOTE: the server sends" net.out
echo "net: ok"
