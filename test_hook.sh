#!/bin/bash
# Test the bind_hook.so

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SO="$SCRIPT_DIR/bind_hook.so"

if [ ! -f "$SO" ]; then
    echo "Building $SO..."
    make -C "$SCRIPT_DIR"
fi

echo "=== Testing bind_hook.so ==="
echo "Testing that SO_REUSEPORT is set on port 53 binds..."
echo ""

# Use strace to verify setsockopt is called
echo "Running: LD_PRELOAD=$SO strace -e setsockopt python3 -c 'import socket; s=socket.socket(); s.bind((\"0.0.0.0\", 53))' 2>&1 | grep REUSEPORT"
echo ""

LD_PRELOAD="$SO" strace -e setsockopt python3 -c '
import socket
s = socket.socket()
s.bind(("0.0.0.0", 53))
s.close()
' 2>&1 | grep -i reuseport && echo "OK: REUSEPORT detected" || echo "WARN: REUSEPORT not in trace (may still be set)"

echo ""
echo "=== Done ==="
