# Guía · ZX Spectrum 48K para Cardputer ADV

## Instalar

Necesitas un Cardputer ADV, una microSD FAT32, una ROM de Spectrum 48K de
16384 bytes y tus juegos de Spectrum. Esta versión no contiene la ROM ni juegos.

1. Copia tu ROM con el nombre exacto `/SPECTRUM/48.ROM`.
2. Copia los juegos en las carpetas de su formato:

   ```text
   SPECTRUM/
   ├── 48.ROM
   └── GAMES/
       ├── TAP/
       ├── TZX/
       ├── SNA/
       └── Z80/
   ```

   Puedes extraer el ZIP `Spectrum48K-CardputerADV-V0.3-SD.zip` en la raíz de
   la tarjeta: incluye las carpetas y el BIN, sin ROM ni juegos. Las carpetas
   de Amstrad pueden convivir con estas en la misma SD.
3. Si tienes M5Launcher, copia `Spectrum48K-CardputerADV-V0.3-app.bin` y elige
   instalarlo desde Launcher. Conserva el firmware de Amstrad en la SD para
   poder cambiar entre ambos con Launcher.
4. Reinicia y espera el arranque de BASIC. Si falla SD o ROM aparece un mensaje.
5. Pulsa Tab, selecciona un archivo con las flechas y pulsa Enter.

V0.2 organiza los juegos con el mismo criterio que Amstrad. En cada arranque
crea `GAMES/TAP`, `TZX`, `SNA` y `Z80` aunque `GAMES` ya existiera, y mueve a
ellas los archivos sueltos de `GAMES`. También separa las instantáneas que
había en `GAMES/SNAPSHOTS`. Si el destino ya existe, conserva ambos archivos
en sus ubicaciones; no los sobrescribe. Respeta las subcarpetas personales.
El selector oculta los auxiliares `._` de macOS y mantiene visibles las
carpetas aunque una lista de juegos supere el límite de 128 entradas.

V0.3 mantiene esa estructura. Para actualizar desde V0.2 basta con instalar
el BIN V0.3; no necesitas cambiar la ROM ni volver a copiar los juegos.

El BIN es solo la aplicación. Para una primera instalación sin Launcher,
compila y sube con Arduino IDE/CLI: así se generan también el arranque y la
tabla de particiones. Con el perfil independiente `default_8MB`, la aplicación
queda en `0x10000`; sobre una placa con Launcher deja que Launcher elija la
partición. No uses este app.bin como imagen completa para grabar en `0x0`.

## Compilar con Arduino

Instala el paquete M5Stack y las bibliotecas M5Cardputer, M5Unified y M5GFX.
El entorno utilizado ya tenía estas dependencias:

| Componente | Versión |
| --- | --- |
| M5Stack ESP32 | 3.3.10 |
| M5Cardputer | 1.1.1 |
| M5Unified | 0.2.28 |
| M5GFX | 0.2.35 |

Abre `Spectrum48KCardputerADV.ino`, selecciona M5Cardputer y configura PSRAM
Disabled, QIO, 8 MB, Default 8 MB, Hardware CDC/JTAG y USB CDC al arrancar.

Con Arduino CLI, desde la carpeta del proyecto:

```sh
sh scripts/build.sh
```

Si CLI no está en PATH puedes indicar `SPECTRUM_ARDUINO_CLI`. En macOS el script
también reconoce la copia incluida en Arduino IDE. La salida queda en
`.build-verified/`; el BIN y su suma se copian a `firmware/`.

## Cargar una cinta

En el selector, O selecciona la velocidad original; R activa la rápida y S
activa o silencia el monitor de cassette. La selección persiste durante la
sesión, no tras apagar.

Al elegir TAP/TZX, el emulador reinicia, deja arrancar la ROM, pulsa la secuencia
de teclado para `LOAD ""` y pone la cinta en marcha unos seis segundos después.
La pantalla de carga y los bordes proceden de la emulación. La reproducción
continúa entre bloques: para cargas múltiples usa Fn+T para pausar y reanudar
cuando el juego lo solicite.

Un TZX puede incluir una orden de parada. Reanúdalo con Fn+T. Si aparece
«Bloque TZX no implementado», utiliza una edición con un cargador compatible
o una instantánea de 48K. Los bloques CSW y generalized data no están soportados.

La secuencia `LOAD ""` se ha verificado en ordenador con una ROM 48K y un
programa BASIC que se carga y se ejecuta desde TAP y TZX. V0.2 conserva durante
1 ms el último flanco antes de bajar la señal en la pausa, corrigiendo el fallo
de checksum de V0.1 que impedía completar la carga. Para repetir la orden
manualmente, pausa la cinta con Fn+T, pasa a
teclado BASIC con Fn+J, introduce la orden y vuelve a pulsar Fn+T.

## Teclado de BASIC

El Spectrum original introduce muchas palabras con una sola tecla. Al comienzo
de una línea, `P` introduce `PRINT`, `J` introduce `LOAD` y `R` introduce `RUN`.
Las letras que figuran aquí pueden pulsarse en minúscula, sin Shift.

Para probar `PRINT "HOLA"`, pulsa `P`, escribe las comillas y el texto y pulsa
Enter. Para `LOAD ""`, pulsa `J`, dos comillas y Enter. Los signos de puntuación
se traducen a SYMBOL SHIFT más la tecla equivalente del Spectrum.

Shift corresponde a CAPS SHIFT; Ctrl o Alt corresponde a SYMBOL SHIFT.
Retroceso envía CAPS SHIFT+0; Esc envía CAPS SHIFT+espacio (BREAK). Las flechas
de la capa Fn, en modo BASIC, envían CAPS SHIFT+5/6/7/8. Fn+J cambia entre teclado
de BASIC y los controles de juego.

## Jugar

Después de cargar se activa el modo juego. Las teclas `;`, `.`, `,`, `/`, que
llevan las flechas impresas, funcionan sin mantener Fn. Espacio dispara.
Fn+K cambia el tipo de control:

| Modo | Lo que recibe el Spectrum |
| --- | --- |
| Kempston | Joystick del puerto 0x1F |
| Sinclair 1 | Teclas 6/7/8/9/0 |
| Cursor | CAPS SHIFT+5/6/7/8; 0 para disparar |
| QAOP | Q/A/O/P y espacio |

Elige ese mismo modo en el menú del juego. Para redefinir teclas o escribir
puntuación, usa Fn+J para volver temporalmente al teclado de BASIC.

## Capturas y diagnóstico

El puerto USB acepta estos caracteres, sin necesitar Enter:

| Carácter | Resultado |
| --- | --- |
| `?` | PC, fotogramas, posición de cinta, heap y paquetes de audio descartados |
| `r` | Reiniciar en BASIC |
| `m` | Abrir el selector |
| `c` | Captura RGB565 de la imagen emulada a 240×135 |
| `b` | Prueba de tono por Z80 durante unos cinco segundos; después continúa el estado anterior |

V0.3 separa la generación de muestras de la tarea que las entrega al altavoz.
La reserva de unos 40 ms absorbe pausas cortas de la SD o del teclado. La
velocidad original se regula con el reloj de audio de 16 kHz, mientras la LCD
se actualiza de forma independiente. Fn+F sigue activando el modo rápido y
silenciando el audio.

El diagnóstico `?` incluye `underruns` (cola sin datos durante más de 20 ms),
`dropped` (muestras descartadas por cola llena), `audio_errors`, `gap_us` y
`pace_timeouts`. También muestra el tiempo de CPU, teclado, filtro LCD, SPI y
la lectura más lenta de la SD. Un salto en estos contadores ayuda a localizar
el componente que se atasca; no equivale a una medición acústica del altavoz.

En macOS/Linux puedes leerlos sin dependencias adicionales:

```sh
python3 scripts/device-diagnostics.py /dev/cu.usbmodemTU_PUERTO '?' --seconds 10
python3 scripts/device-diagnostics.py /dev/cu.usbmodemTU_PUERTO b
```

La segunda orden pausa temporalmente la máquina actual, ejecuta un Z80
sintético con un tono, teclado y pantalla activos, informa de los contadores
y continúa la máquina anterior. No modifica ROMs ni juegos de la SD.

Para guardar PNG desde el dispositivo, con pyserial y Pillow disponibles:

```sh
python3 scripts/capture-device.py /dev/cu.usbmodemTU_PUERTO captura.png
```

La captura contiene la imagen emulada, sin los avisos superpuestos de volumen
o controles. La transferencia pausa brevemente emulación y audio. Cierra antes
el monitor serie y el selector.

## Pruebas en ordenador

```sh
sh tests/run_host.sh
```

Necesitas un compilador C/C++ con AddressSanitizer y UBSan. La prueba crea
medios sintéticos y una imagen PPM en `tests/.build/synthetic-screen.ppm`.
No utiliza ROMs comerciales y no certifica compatibilidad con juegos reales.

Para la prueba opcional con tu ROM de 48K:

```sh
sh tests/run_host.sh /ruta/a/48.ROM
```

Además de las pruebas sintéticas, ejecuta LD-BYTES con cabeceras y datos,
rechaza un checksum incorrecto y carga un programa BASIC desde TAP y TZX hasta
verificar su ejecución. La ROM solo se lee de esa ruta y no se distribuye.

`tests/realtime.cpp` comprueba la propiedad de los paquetes entre dos hilos,
60 segundos de audio con pausas simuladas de 25 ms, la frecuencia exacta de
muestras y la coincidencia de todos los píxeles con el filtro anterior.
`sh tests/run_benchmark.sh` mide CPU y escalado en el ordenador; esos tiempos
no son una estimación de los tiempos del ESP32.

[Estado de validación](../VALIDACION.md) ·
[Documentación oficial del Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv).
