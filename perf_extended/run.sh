#!/bin/bash
set -e

APP=/Users/davidstan/Documents/licenta/Licenta/perf_demo
BOARD=nucleo_f429zi
PORT=/dev/cu.usbmodem103

cd ~/zephyrproject/zephyr

echo ""
echo "=============================="
echo "  Step 1: BARE build (no VMMU)"
echo "=============================="
env -u CPPFLAGS -u LDFLAGS west build -b $BOARD $APP --pristine
west flash --runner openocd

echo ""
echo "Connect serial: picocom -b 115200 $PORT"
echo "Record the cycle counts, then press ENTER to continue..."
read -r

echo ""
echo "=============================="
echo "  Step 2: VMMU build"
echo "=============================="
env -u CPPFLAGS -u LDFLAGS west build -b $BOARD $APP --pristine -- -DPERF_VMMU=ON
west flash --runner openocd

echo ""
echo "Connect serial: picocom -b 115200 $PORT"
echo "Record the cycle counts."
echo ""
echo "Overhead = (vmmu_total - bare_total) / bare_total * 100%%"
