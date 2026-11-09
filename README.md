# Sinclair ZX Spectrum 48K para Cardputer ADV

Emulador del **ZX Spectrum 48K clásico**, el de teclas de
goma, para **M5Stack Cardputer ADV**. Proyecto C++/Arduino independiente del
emulador de Amstrad CPC 464.

**V0.2 · compilación y pruebas de núcleo verificadas · pendiente de prueba física.**

[Firmware para M5Launcher](firmware/Spectrum48K-CardputerADV-V0.2-app.bin) ·
[ZIP con estructura de SD](firmware/Spectrum48K-CardputerADV-V0.2-SD.zip) ·
[Guía en español](docs/GUIA.md) · [Validación y límites](VALIDACION.md)

**V0.2 corrige la carga TAP que terminaba sin arrancar el programa.** La pausa
entre bloques conservaba mal el último flanco del checksum. Se ha reproducido
el fallo y verificado la corrección con la rutina LD-BYTES de una ROM 48K y con
un programa BASIC que carga y se ejecuta desde TAP y TZX.

## Incluye

- Z80, ROM de 16 KiB y RAM de 48 KiB. Memoria reservada sin necesitar PSRAM.
- Arranque en Sinclair BASIC usando tu ROM desde microSD.
- Imagen ULA de 256×192 con INK, PAPER, BRIGHT y FLASH; reducción filtrada a
  240×135 para conservar trazos finos. Bordes visibles durante la carga.
- Selector con subcarpetas, seis entradas por página y archivos auxiliares
  de macOS ocultos.
- Cintas **TAP** y bloques habituales de **TZX**, por streaming. El Z80 lee
  los pulsos EAR: el borde lo genera el propio programa de carga.
- Carga automática de cinta con `LOAD ""`, y pausa/reanudación manual.
- Instantáneas **SNA de 48K** y **Z80 v1/v2/v3 de 48K**, con RLE.
- Beeper y monitor de cassette; volumen inicial del 30%.
- Controles Kempston, Sinclair 1, Cursor y QAOP+espacio.
- Capturas USB y pruebas en ordenador sin ROMs ni juegos.

## Preparar la tarjeta

```text
/SPECTRUM/48.ROM                exactamente 16384 bytes
/SPECTRUM/GAMES/TAP/            tus cintas .tap
/SPECTRUM/GAMES/TZX/            tus cintas .tzx
/SPECTRUM/GAMES/SNA/            tus .sna
/SPECTRUM/GAMES/Z80/            tus .z80
```

El mismo esquema que en Amstrad: ROM en la carpeta del sistema y juegos por
formato dentro de `GAMES/`. Al arrancar se crean las cuatro subcarpetas aunque
`GAMES/` ya existiera. Los juegos sueltos de `GAMES/` y las instantáneas de la
antigua carpeta `SNAPSHOTS/` se trasladan a la carpeta de su formato. Las
colisiones se conservan sin sobrescribir y las subcarpetas personales siguen
accesibles. La ROM y los juegos **no se
incluyen** en el código ni en el BIN. Los archivos de Amstrad no sirven para
esta máquina, aunque algunas extensiones se parezcan.

Para M5Launcher, copia el archivo `firmware/Spectrum48K-CardputerADV-V0.2-app.bin`
a la tarjeta e instálalo desde Launcher. Es un **BIN de aplicación**, no una
imagen de flash completa. [Instalación detallada](docs/GUIA.md#instalar).

## Controles

| Tecla | Acción |
| --- | --- |
| Tab / botón G0 | Abrir o cerrar el selector |
| `;` / `.` en el selector | Subir / bajar una entrada |
| `,` / `/` en el selector | Página anterior / siguiente |
| Enter | Entrar en carpeta o cargar |
| Retroceso / `[..] Volver` | Carpeta anterior |
| O / R en el selector | Velocidad original / rápida |
| S en el selector | Monitor de cassette ON/OFF |
| Fn+F | Alternar velocidad; el modo rápido silencia el audio |
| Fn+B | Borde permanente / borde automático durante la carga |
| Fn+J | Controles de juego / teclado de BASIC |
| Fn+K | Cambiar Kempston → Sinclair 1 → Cursor → QAOP |
| Fn+T | Pausar / reanudar cinta |
| Fn+R | Reiniciar en BASIC y expulsar la cinta |
| Fn+= / Fn+- | Volumen, pasos de unos 5 puntos porcentuales |
| Flechas impresas, sin Fn, en modo juego | Mover el joystick seleccionado |
| Espacio, en modo juego | Disparo según el tipo de control |
| Shift / Ctrl o Alt | CAPS SHIFT / SYMBOL SHIFT |

Al cargar un juego se activan los controles de juego. En el menú del juego
selecciona el mismo joystick que hayas elegido con Fn+K. En BASIC las teclas
conservan los tokens originales: por ejemplo, `J` produce `LOAD` al inicio de
una línea. [Ejemplos y teclado](docs/GUIA.md#teclado-de-basic).

## Compilar y comprobar

Mismo perfil que el proyecto de Amstrad: ESP32-S3, flash QIO de 8 MB, PSRAM
desactivada y USB CDC al arrancar. Dependencias del entorno usado: M5Stack
ESP32 3.3.10, M5Cardputer 1.1.1, M5Unified 0.2.28 y M5GFX 0.2.35.

```sh
sh scripts/build.sh
sh tests/run_host.sh
```

El primer script compila y actualiza el BIN y `SHA256SUMS`. El segundo comprueba
el núcleo con AddressSanitizer y UBSan. No instala nada en el Cardputer.

Prueba de carga completa con tu ROM de 48K, que no se copia al repositorio:

```sh
sh tests/run_host.sh /ruta/a/48.ROM
python3 scripts/package-sd.py
```

## Alcance de V0.2

Se han validado en ordenador el arranque de una ROM 48K y la carga y ejecución
de un programa BASIC desde TAP y TZX. **Faltan juegos comerciales y la prueba
física de V0.2 en el dispositivo**. No se afirma compatibilidad general ni velocidad real
garantizada. Faltan contención de memoria, floating bus, efectos raster precisos,
Spectrum 128K/AY, guardado y algunos bloques TZX. El modo rápido acelera el reloj
de emulación tanto como permita el hardware; no sustituye la carga por pulsos
por una carga instantánea. [Lista exacta](VALIDACION.md).

## Créditos

CPU basada en [superzazu/z80](https://github.com/superzazu/z80), de Nicolas
Allemand, licencia MIT. El port conserva las direcciones completas de 16 bits
de los puertos, necesarias para el teclado del Spectrum.
[Procedencia y cambios](THIRD_PARTY_NOTICES.md).
