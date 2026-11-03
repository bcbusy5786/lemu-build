#!/bin/sh
# Run the Linux QEMU of a PowerPC kit (40P, 7006, pSeries) in a clean
# ubuntu:24.04 container: print its version and displays, then boot the
# kit for 40 s. Use a copy of the unpacked kit: the boot writes to its disk.
# usage: ltest-ppc.sh amd64|arm64 KITCOPY qemu-system-ppc|qemu-system-ppc64
[ $# -eq 3 ] || { echo "usage: $0 amd64|arm64 KITCOPY BINARY" >&2; exit 1; }
arch=$1 kit=$(cd "$2" && pwd) b=$3
docker run --rm --platform "linux/$arch" -v "$kit:/kit" ubuntu:24.04 sh -c "apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libx11-6 >/dev/null 2>&1
cd /kit
q=bin/linux-\$(uname -m)/$b
\$q --version | head -1
\$q -display help | tail -n +2 | tr '\n' ' '; echo
SDL_VIDEODRIVER=offscreen timeout 40 ./run.sh < /dev/null > /tmp/out.txt 2>&1
echo \"run exit \$? (124 = still running after 40 s, which is good)\"
grep -v 'guest is now late' /tmp/out.txt | tr -d '\r' | head -5"
