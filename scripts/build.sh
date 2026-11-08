#!/bin/sh
set -eu
zx_root=$(cd "$(dirname "$0")/.." && pwd)
zx_cli=${SPECTRUM_ARDUINO_CLI:-arduino-cli}
if ! command -v "$zx_cli" >/dev/null 2>&1; then
  if [ -x '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' ]; then
    zx_cli='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
  else
    echo 'Instala Arduino CLI o define SPECTRUM_ARDUINO_CLI.' >&2
    exit 1
  fi
fi
zx_fqbn='m5stack:esp32:m5stack_cardputer:PSRAM=disabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc'
"$zx_cli" compile --fqbn "$zx_fqbn" --build-path "$zx_root/.build-verified" "$zx_root"
cp "$zx_root/.build-verified/Spectrum48KCardputerADV.ino.bin" "$zx_root/firmware/Spectrum48K-CardputerADV-V0.1-app.bin"
cd "$zx_root/firmware"
shasum -a 256 Spectrum48K-CardputerADV-V0.1-app.bin > SHA256SUMS
