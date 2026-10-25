#!/bin/bash
# Run the x4pro build in the esp32sim submodule's emulator, natively or in the browser.
#
#   scripts/emu_x4pro.sh sd BOOKS_DIR [SIZE_MB]  build .pio/emu/x4pro-sd.img (MBR + FAT) holding BOOKS_DIR
#   scripts/emu_x4pro.sh web                     stage the build and card for esp32sim's page; prints the URL
#   scripts/emu_x4pro.sh cli [esp32sim args...]  boot the build in the native emulator with the card
#
# Build the firmware first (pio run -e x4pro) and the emulator (cargo build --release in esp32sim;
# tools/wasm-build.sh there for the page). The card is written by scripts/fat_image.py: FAT16 up
# to 2 GB, FAT32 above; without SIZE_MB it is sized to fit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIM="$ROOT/esp32sim"
BUILD="$ROOT/.pio/build/x4pro"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
CARD="$ROOT/.pio/emu/x4pro-sd.img"

need() { for f in "$@"; do [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }; done; }

case "${1:-}" in
  sd)
    books="${2:?usage: $0 sd BOOKS_DIR [SIZE_MB]}"
    [ -d "$books" ] || { echo "not a directory: $books" >&2; exit 1; }
    mkdir -p "$(dirname "$CARD")"
    python3 "$ROOT/scripts/fat_image.py" "$CARD" "$books" ${3:+"$3"}
    ;;
  web)
    need "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$CARD" "$SIM/web/wasm/esp32sim.wasm"
    # web/wasm/fw/.gitignore ignores everything it does not list: none of this is committed
    dest="$SIM/web/wasm/fw/local/x4pro"; mkdir -p "$dest"
    cp "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$dest/"
    cp "$CARD" "$dest/sd.img"
    cat > "$SIM/web/wasm/fw/x4pro.json" <<'EOF'
{
 "board": "xteink-x4pro",
 "flash_mb": 16,
 "psram_mb": 8,
 "files": {
  "rom": "esp32s3_rev0_rom.elf",
  "bootloader": "local/x4pro/bootloader.bin",
  "ptable": "local/x4pro/partitions.bin",
  "app": "local/x4pro/firmware.bin",
  "sd": "local/x4pro/sd.img"
 },
 "flash_at": { "0xe000": "local/x4pro/boot_app0.bin" },
 "_note": "Local only (not committed): scripts/emu_x4pro.sh web"
}
EOF
    echo "serve:  python3 -m http.server -d \"$SIM/web\" 8790"
    echo "open:   http://127.0.0.1:8790/run.html?wasm&fw=x4pro"
    ;;
  cli)
    shift
    need "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0" "$CARD" "$SIM/target/release/esp32sim"
    cd "$SIM"
    exec ./target/release/esp32sim --board xteink-x4pro --boot rom --rom web/wasm/fw/esp32s3_rev0_rom.elf \
      --bootloader "$BUILD/bootloader.bin" --ptable "$BUILD/partitions.bin" --app "$BUILD/firmware.bin" \
      --flash-at "0xe000=$BOOT_APP0" --sd "$CARD" "$@"
    ;;
  *)
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 2
    ;;
esac
