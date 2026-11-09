# Validación · V0.2 · 9 de noviembre de 2026

## Corrección de la carga TAP

Se reprodujo el fallo de V0.1 con la rutina LD-BYTES de una ROM 48K: tras recibir
257 bytes, la CPU volvía al llamador con IX=0x9101, DE=0 y carry=0 (carga fallida).
La transición a la pausa bajaba EAR en el mismo instante que el último flanco
del checksum, cancelándolo. También se forzaba el nivel a cero al terminar el
archivo, incluso cuando no había pausa.

V0.2 conserva ese flanco durante 1 ms antes de bajar EAR en la pausa. Una pausa
cero o un fin de archivo conserva el nivel final. Con la corrección, la misma
prueba vuelve con carry=1 y los 257 bytes coinciden con los esperados. Se han
añadido una regresión sintética del último flanco y pruebas opcionales de carga
con una ROM externa, sin incluir la ROM en el proyecto ni en el firmware.

## Resultado comprobado

- Compilación Arduino para **M5Stack Cardputer ADV / ESP32-S3**, finalizada sin errores.
- Programa: **637815 bytes**, 19% del espacio de aplicación del perfil utilizado.
- Variables globales: **102204 bytes**, 31% de SRAM; quedan 225476 bytes antes
  de las reservas dinámicas de los controladores y de las tareas.
- BIN de aplicación: **637968 bytes**. `esptool image-info` reconoce ESP32-S3,
  flash de 8 MB y valida checksum y hash internos.
- `shasum -a 256 -c SHA256SUMS`: **OK**.
- `sh tests/run_host.sh`: **OK**, con AddressSanitizer y UBSan.
- Prueba opcional `sh tests/run_host.sh /ruta/a/48.ROM`: **OK** en el entorno
  de desarrollo, incluida la carga y ejecución desde TAP y TZX.
- Sintaxis del script Python de captura y de los scripts shell comprobada.
- El proyecto Amstrad existente conserva su estado limpio de Git.

Perfil de compilación:

```text
m5stack:esp32:m5stack_cardputer:PSRAM=disabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc
```

El perfil selecciona QIO; la cabecera de la aplicación producida por el core
M5Stack figura como DIO en `esptool`. No se ha modificado esa cabecera ni se ha
generado una imagen fusionada de flash. El BIN es para instalar desde Launcher
o junto con el arranque y las particiones generados por Arduino.

Dependencias locales: M5Stack ESP32 3.3.10, M5Cardputer 1.1.1,
M5Unified 0.2.28 y M5GFX 0.2.35. El entorno local de M5Unified contiene
modificaciones previas del proyecto de Amstrad; este port utiliza su API pública
de audio y no requiere los callbacks añadidos localmente. Falta reproducir la
compilación en una instalación limpia de esas dependencias.

## Pruebas del núcleo

Se ejecutan en ordenador con medios y programas generados en las pruebas,
sin una ROM comercial ni juegos:

| Área | Comprobación |
| --- | --- |
| Memoria | Lectura de ROM; escrituras protegidas por debajo de 0x4000; RAM escribible |
| CPU y puertos | Programa Z80 ejecutado; IN inmediato y por BC conservan el byte alto |
| Teclado | Filas seleccionadas activas a nivel bajo; combinación de filas; signos y retroceso |
| Joystick | Kempston, Sinclair y Cursor |
| Reloj | Frame de 69888 T-states y aceptación de interrupción PAL desde HALT |
| SNA | Registros y retorno PC desde pila; rechazo de SP inválido sin cambiar la máquina |
| Z80 | v1 sin compresión y con RLE; v2 y dos longitudes de cabecera v3; páginas 8/4/5 |
| Archivos inválidos | RLE con longitud cero, truncados, cabeceras cortas y modelos 128K rechazados |
| TAP | Duraciones de piloto, sincronización y datos; pausa/reanudación |
| Último flanco | Se conserva la transición final del checksum durante 3500 T-states antes de bajar EAR |
| TZX | Datos puros, turbo, tonos, bucles, grabación directa y parada/reanudación |
| Integración cassette | Programa Z80 lee EAR y produce cambios de borde ULA |
| Vídeo | Dirección entrelazada de bitmap, atributos, BRIGHT, FLASH y colores RGB565 |
| Reducción de imagen | Recorrido completo a 240×135, con y sin bordes, bajo sanitizadores |
| Carpetas | Creación sobre SD vacía y existente; TAP/TZX/SNA/Z80; migración de SNAPSHOTS |
| Organización | 270 archivos en varias tandas; archivos ocultos excluidos; colisiones y errores preservan el origen |
| Navegación | Carpetas visibles con más de 128 archivos; subcarpetas, volver y páginas de seis filas |

Las pruebas generan `tests/.build/synthetic-screen.ppm`, un patrón de ULA.
**Es una prueba sintética, no una captura de BASIC ni del dispositivo.**

## Pruebas con la ROM 48K en ordenador

Se leyó una ROM de referencia de 16384 bytes desde una carpeta temporal,
procedente del árbol de ROMs del proyecto Fuse. SHA-256:
`d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`.
La ROM no se ha copiado al proyecto. Los bloques de cinta y el programa BASIC
se generan en memoria dentro de `tests/rom_tap.cpp`.

- LD-BYTES carga una cabecera de 17 bytes (flag 00) desde TAP y TZX: carry=1.
- LD-BYTES carga 257 bytes de datos (flag FF) desde TAP y TZX: carry=1 y datos exactos.
- Un checksum alterado sigue rechazándose con carry=0.
- Arranque de la ROM, entrada de `LOAD ""` por la matriz de teclado, lectura
  de cabecera y datos, autostart en línea 10 y ejecución desde TAP y TZX: OK.
- El programa ejecuta `BORDER VAL "4": POKE VAL "32768",VAL "42": STOP`.
  Se comprueban el valor 42 en 0x8000 y el borde 4. El código 8 de ERR_NR es
  el STOP deliberado del programa de prueba.

Estas pruebas ejecutan el cargador ROM a partir de los pulsos EAR; no usan
interceptación de ROM ni carga instantánea. No certifican juegos comerciales.

## Formatos implementados

| Formato | Alcance |
| --- | --- |
| TAP | Bloques estándar con piloto, sincronización, datos y pausa |
| TZX | IDs 10, 11, 12, 13, 14, 15, 20, 21, 22, 24, 25, 2A, 2B, 30, 31, 32, 33, 35, 5A |
| SNA | 48K, exactamente 49179 bytes; PC recuperado de la pila |
| Z80 | v1/v2/v3, máquina 48K simple; RLE y páginas sin comprimir |
| ROM | ROM externa de 48K de 16384 bytes |

TZX se analiza a medida que avanza la cinta: un bloque no implementado situado
más adelante puede detener una carga que ya haya empezado. Los metadatos se
omiten; los bucles se permiten hasta cuatro niveles. Las paradas se reanudan
manualmente con Fn+T. Las cabeceras de expansión, SamRam y 128K se rechazan.

Las instantáneas se leen primero en un espacio temporal de 48 KiB. Solo se
aplican después de validar el archivo completo: un archivo corrupto o truncado
no sustituye el estado de la máquina. Si falta memoria, se muestra un error.

## Pendiente de validar

No se ha instalado V0.2 en un Cardputer durante esta tarea. Quedan pendientes:

1. Arranque físico en Sinclair BASIC y lectura de la SD.
2. Carga completa y ejecución de juegos reales por TAP y TZX en el Cardputer.
3. Reanudación de una carga múltiple desde el juego.
4. Teclado físico, Fn, navegación y los cuatro tipos de control con juegos.
5. Volumen, continuidad y calidad del beeper y del monitor de cinta.
6. Medición de velocidad real, uso de heap y carga de la LCD en el ESP32-S3.
7. Instalación desde M5Launcher y captura USB real.

La emulación y la LCD tienen tareas separadas; se publican copias de los 6912
bytes de pantalla y de los bordes. Los paquetes de audio usan tres buffers
persistentes y se cuentan los descartes en el diagnóstico USB. Esto describe
la implementación; todavía no acredita rendimiento ni audio correctos en hardware.

## Límites conocidos

- Sin contención de RAM/puertos, floating bus ni precisión de píxel en efectos raster.
- Las instrucciones se ejecutan completas: EAR, beeper e interrupciones tienen
  resolución de instrucción, no de T-state individual.
- El borde se conserva por línea; la imagen activa se toma al presentar un frame.
- Sin Spectrum 128K, AY, discos, escritura de cinta ni guardado de instantáneas.
- Sin bloques TZX CSW, generalized data, saltos, llamadas ni selección de alternativas.
- No hay carga instantánea por interceptación de ROM. El modo rápido elimina
  la espera de reloj y silencia el audio, conservando el cargador por pulsos.
- Hasta 128 entradas por carpeta, más la entrada para volver. Usa subcarpetas
  si hay más archivos. ZIP no implementado.
- Los ajustes no se guardan al apagar. Los controles se eligen con Fn+K.
- El autoload espera unos seis segundos de emulación antes de reproducir la
  cinta; la duración deberá ajustarse si una ROM necesita más tiempo de arranque.

**V0.2 corrige un fallo de carga reproducido y está compilada con pruebas de
núcleo y de integración con ROM. La compatibilidad de juegos y la validación
física de esta versión siguen pendientes.**
