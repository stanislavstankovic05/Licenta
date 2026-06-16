# VMMU — Virtual Memory Management Unit software pentru MCU fără MMU

Izolare software a memoriei pentru microcontrollere Cortex-M (fără MMU hardware),
implementată prin instrumentare LLVM + un runtime VMMU peste Zephyr RTOS.
Fiecare acces de memorie din codul aplicației este validat printr-un `vmmu_check`
injectat de un pass LLVM, cu două optimizări: un **Range TLB** (32 intrări) și
**loop range hoisting**.

Validat pe **Nucleo F429ZI** (Cortex-M4F, 180 MHz), Zephyr v4.3.

---

## 1. Cerințe

- **LLVM/Clang** (Homebrew): `brew install llvm` — necesar pentru pass-urile de instrumentare.
- **Zephyr SDK + west** — vezi pasul 2.
- **OpenOCD**: `brew install open-ocd` — pentru flash pe placă.
- **Terminal serial**: `screen` (built-in pe macOS) sau `brew install picocom`.
- Placă **Nucleo F429ZI** conectată pe USB (portul ST-Link, ex. `/dev/cu.usbmodem103`).

## 2. Setup Zephyr (o singură dată)

Zephyr **nu** este inclus în acest repo (este o dependență externă de ~8 GB,
gestionată de `west`). Instalează-l separat:

```bash
pip install west
west init ~/zephyrproject
cd ~/zephyrproject
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
# instalează Zephyr SDK conform https://docs.zephyrproject.org/latest/develop/getting_started/
```

> Proiectul a fost dezvoltat pe Zephyr **v4.3**. Pune acest repo (sau un symlink)
> astfel încât căile din comenzile de mai jos să fie corecte, sau ajustează-le.

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
| `libVMMUPassO0.so` | pass principal — instrumentare + loop hoisting (PipelineStart + mem2reg, merge la -O0 și -O2) |
| `libVMMUPassNoHoist.so` | variantă fără hoisting (baseline per-element pentru măsurători) |
| `libVMMUPass.so` | varianta inițială (înregistrată la VectorizerStart) |

---

## 4. Demo — suita de validare (13 scenarii de izolare)

Demonstrează corectitudinea: out-of-bounds, acces cross-app, demand paging,
stack overflow, izolarea globalelor, TLB hit/invalidare, IPC etc. Un thread
watchdog confirmă că sistemul rămâne funcțional după fiecare crash de aplicație.

```bash
cd ~/zephyrproject/zephyr
env -u CPPFLAGS -u LDFLAGS west build -b nucleo_f429zi <repo>/demo --pristine
west flash --runner openocd
screen /dev/cu.usbmodem103 115200      # iesire: Ctrl-A k y
```

## 5. Suita de workload-uri (evaluarea performanței)

7 workload-uri (`array_fill_sum`, `xor_sweep`, `recursive_sum`, `matrix_dot`,
`packet_io`, `fir_filter`, `stream_xor`). Toate build-urile la `-O2`; se compară
cele 4 configurații, în ordine, notând `cycles`/`time`/`tlb_hits` de pe serial.

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

Verificare rapidă pe parcurs: la (1) `tlb_hits = 0`, la (2) `tlb_hits` în milioane
(per-element), la (3) `tlb_hits` în zeci de mii (hoistat).

**Overhead** = `vmmu_total / bare_total`. Rezultate de referință (suita completă):
naiv **64×** → +TLB **21×** → +TLB+hoisting **3.7×**.

---

## Structura repo-ului

| Director | Conținut |
|---|---|
| `vmmu/` | runtime-ul VMMU (translator, frame allocator, heap, TLB, address space, app) |
| `passes/` | pass-urile LLVM de instrumentare (sursă + CMake) |
| `demo/` | aplicația de validare (13 scenarii) |
| `perf_extended/` | harness-ul de benchmark (7 workload-uri, 4 configurații) |

## Reproducerea graficelor din lucrare

```bash
pip install matplotlib
python <repo>/thesis/plot_results.py
python <repo>/thesis/plot_compare.py
```
