#!/bin/bash
# Diagnostic: test connectivity to ATL atom server from CI

echo "****************************************"
echo "  Testing atom server connectivity"
echo "****************************************"

HOST="gregeisenhauer.online"
PORT=80
PATH_URL="/cgi-bin/korvo_server/v1/atoms"

echo "DNS resolution:"
getent hosts "$HOST" 2>/dev/null || nslookup "$HOST" 2>/dev/null || echo "  (no DNS tools available)"

echo ""
echo "Attempting 20 rapid TCP connections to ${HOST}:${PORT}..."
for i in $(seq 1 20); do
    start=$(date +%s%N 2>/dev/null || python3 -c "import time; print(int(time.time()*1e9))")
    # Try to connect with 5 second timeout
    if command -v timeout >/dev/null 2>&1; then
        timeout 5 bash -c "echo > /dev/tcp/${HOST}/${PORT}" 2>/dev/null
        rc=$?
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c "
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(5)
try:
    s.connect(('${HOST}', ${PORT}))
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
"
        rc=$?
    else
        rc=99
    fi
    end=$(date +%s%N 2>/dev/null || python3 -c "import time; print(int(time.time()*1e9))")
    elapsed_ms=$(( (end - start) / 1000000 ))
    if [ $rc -eq 0 ]; then
        echo "  Connection $i: OK (${elapsed_ms}ms)"
    else
        echo "  Connection $i: FAILED rc=$rc (${elapsed_ms}ms)"
    fi
done

echo ""
echo "Attempting HTTP POST to atom server..."
if command -v curl >/dev/null 2>&1; then
    curl -v -m 10 "${HOST}:${PORT}${PATH_URL}" -X POST \
        -H "Content-Type: application/json" \
        -d '{"string":"ci_test","atom":99999}' 2>&1
elif command -v python3 >/dev/null 2>&1; then
    python3 -c "
import urllib.request, json, time
url = 'http://${HOST}:${PORT}${PATH_URL}'
data = json.dumps({'string':'ci_test','atom':99999}).encode()
req = urllib.request.Request(url, data=data, headers={'Content-Type':'application/json'})
start = time.time()
try:
    resp = urllib.request.urlopen(req, timeout=10)
    print(f'HTTP {resp.status}: {resp.read().decode()} ({(time.time()-start)*1000:.0f}ms)')
except Exception as e:
    print(f'FAILED: {e} ({(time.time()-start)*1000:.0f}ms)')
"
fi
echo ""
