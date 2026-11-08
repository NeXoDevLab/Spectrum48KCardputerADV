# Firmware V0.1

`Spectrum48K-CardputerADV-V0.1-app.bin` es un BIN de aplicación compilado para
M5Stack Cardputer ADV, ESP32-S3, flash QIO de 8 MB, PSRAM desactivada y perfil
`default_8MB`, USB Hardware CDC/JTAG con CDC al arrancar.

**Compilado; pendiente de instalación y prueba física.** No contiene ROM ni juegos.

- Con M5Launcher: instalar el BIN desde SD y dejar que Launcher elija la partición.
- Primera instalación: usar Arduino IDE/CLI, que genera también bootloader y particiones.
- Instalación independiente `default_8MB`: la aplicación va en `0x10000`.
- No es una imagen completa para grabar en `0x0`.
- Integridad: `shasum -a 256 -c SHA256SUMS` desde esta carpeta.

[Guía](../docs/GUIA.md) · [Pruebas y límites](../VALIDACION.md).
