# VMMU — Virtual Memory Management Unit software pentru MCU fără MMU

Platforma: **Nucleo F429ZI** (Cortex-M4F, 180 MHz), Zephyr v4.3.

---

## 1. Cerinte

- **LLVM/Clang** (Homebrew): `brew install llvm` 
- **Zephyr SDK + west** 
- **OpenOCD**: `brew install open-ocd` — pentru flash pe placă.
- **Terminal serial**: `screen` (built-in pe macOS) sau `brew install picocom`.
- Placă **Nucleo F429ZI** conectată pe USB (portul ST-Link, ex. `/dev/cu.usbmodem103`).

## 2. Setup Zephyr

```bash
pip install west
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
# instalează Zephyr SDK conform https://docs.zephyrproject.org/latest/develop/getting_started/
```

> Proiectul a fost dezvoltat pe Zephyr **v4.3**.

## 3. Build pass-urile LLVM (o singură dată după modificări)

```bash
cd <repo>/passes
cmake -S . -B build
cmake --build build
# produce: build/libVMMUPass.so, libVMMUPassO0.so, libVMMUPassNoHoist.so,
#          libHeapBoundPass.so, libGlobalsPass.so
```

| Pass | Rol |
|---|---|
| `libVMMUPassO0.so` | pass principal — instrumentare + loop hoisting |
| `libVMMUPassNoHoist.so` | variantă fără hoisting (baseline per-element pentru măsurători) |
| `libVMMUPass.so` | varianta inițială (înregistrată la VectorizerStart) |

---

## 4. Demo — suita
```bash
cd ~/zephyrproject/zephyr
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi <repo>/demo --pristine
west flash --runner openocd
screen /dev/cu.usbmodem103 115200      # iesire: Ctrl-A k y
```

## 5. Evaluarea performanței)

```bash
cd ~/zephyrproject/zephyr
APP=<repo>/perf_extended

# 0. Bare (fără VMMU)
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi $APP --pristine
west flash --runner openocd && screen /dev/cu.usbmodem103 115200

# 1. VMMU naiv (per-element, fără TLB)
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi $APP --pristine \
    -- -DPERF_VMMU=ON -DPERF_NO_TLB=ON -DPERF_NO_HOIST=ON
west flash --runner openocd && screen /dev/cu.usbmodem103 115200

# 2. VMMU + Range TLB (per-element)
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi $APP --pristine \
    -- -DPERF_VMMU=ON -DPERF_NO_HOIST=ON
west flash --runner openocd && screen /dev/cu.usbmodem103 115200

# 3. VMMU + Range TLB + loop hoisting (final)
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi $APP --pristine \
    -- -DPERF_VMMU=ON
west flash --runner openocd && screen /dev/cu.usbmodem103 115200
```
