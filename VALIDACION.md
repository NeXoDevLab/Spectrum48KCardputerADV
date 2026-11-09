# Validación · V0.3 · 9 de noviembre de 2026

## Audio y rendimiento de V0.3

V0.2 entregaba el PCM desde el callback de emulación en paquetes de 320
muestras. El generador podía quedarse esperando al siguiente frame antes de
completar un paquete de 20 ms, y descartaba paquetes si el altavoz ya tenía dos
en cola. El contador antiguo solo indicaba descartes, no falta de muestras.

V0.3 utiliza una FIFO de un productor y un consumidor. El callback publica
paquetes de 160 muestras sin llamar al altavoz. Una tarea de prioridad 3 en
core 0 alimenta M5.Speaker; su tarea I2S tiene prioridad 4 y la LCD prioridad 1.
La emulación conserva unos 40 ms de muestras y se regula con el consumo de
audio. Las esperas largas tienen un límite y un contador; no bloquean el bucle
indefinidamente. Al abrir el menú, capturar o cambiar de juego se detiene el
consumidor antes de vaciar la cola y reutilizar sus buffers.

El filtro LCD separa la reducción horizontal y vertical, decodifica cada
atributo una vez por ocho píxeles y reutiliza tres filas. Los pesos reducidos
permiten sustituir las divisiones por desplazamientos sin cambiar el redondeo
RGB565. Se envían bloques de cuatro filas a la pantalla. La lectura SD pasa de
512 a 2048 bytes y evita buscar de nuevo si el archivo ya está en la posición
correcta. El núcleo y el intérprete Z80 usan O3 solo en el ESP32.

Pruebas adicionales con AddressSanitizer y UBSan:

- Propiedad y orden de 1,92 millones de muestras entre dos hilos; wraparound,
  cola llena y borrado de un paquete parcial.
- Simulación de 60 segundos a 16 kHz con fracciones de frame PAL y pausas de
  25 ms: 959840 muestras consumidas, cero huecos y cero descartes.
- 500 frames producen exactamente 159744 muestras. El modo rápido omite audio.
- Todos los píxeles de cinco pantallas aleatorias coinciden con la referencia,
  con y sin borde y con ambos estados de FLASH.
- Siguen pasando SNA/Z80, organización de carpetas y las cargas TAP/TZX con ROM.

La comparación en el ordenador de desarrollo produjo estos tiempos de filtro:

| Vista | V0.2 | V0.3 | Resultado |
| --- | --- | --- | --- |
| Área activa | 0,409 ms/frame | 0,143 ms/frame | 2,9 veces más rápido |
| Con borde | 0,398 ms/frame | 0,148 ms/frame | 2,7 veces más rápido |

Los hashes RGB565 coinciden (`b5ad398d` y `a88aff25`). Son mediciones en el
ordenador, no equivalencias de velocidad del ESP32; la prueba es reproducible
con `tests/run_benchmark.sh` y su argumento opcional de árbol de fuentes.

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
- Programa: **677467 bytes**, 20% del perfil; BIN de aplicación: **677616 bytes**.
- Variables globales: **122764 bytes**, 37% de SRAM; quedan 204916 bytes antes
  de las reservas dinámicas de los controladores y de las tareas.
- `esptool` reconoce ESP32-S3 y flash de 8 MB y verifica el hash al instalar.
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

## Validación física

V0.3 se ha instalado en el Cardputer ADV conectado. Antes se guardó una copia
de la aplicación y se verificó que coincidía byte por byte con V0.2. La tabla
de particiones y ambas entradas OTA confirmaron que Spectrum era la aplicación
activa, en la partición `spectr` de 1 MiB. Se escribió solo esa aplicación y
esptool confirmó el hash. No se modificaron Launcher, otras apps, NVS ni la SD.

El dispositivo arranca con la ROM de la SD y responde como `ZX-ADV V0.3`.
La prueba USB `b` ejecuta un Z80 sintético durante 250 frames con audio, teclado
y LCD activos, y después reanuda la máquina anterior. Los contadores miden la
continuidad de la entrega; no sustituyen una evaluación acústica de cada juego.

Resultados de la compilación final, instalada y con hash verificado:

| Medida física | Resultado |
| --- | --- |
| Prueba Z80 con tono, teclado y LCD | 250 frames en 4953 ms; la reserva inicial permite adelantar algunos frames |
| CPU del tono | Media 6462 µs/frame; máximo 7638 µs |
| Entrega de audio | 496 paquetes; cero underruns, descartes, errores o timeouts |
| Mayor separación de entregas | 11041 µs, para paquetes de 10 ms |
| Filtro LCD durante el tono | 20267 µs; antes de la optimización final superaba 51 ms |
| SPI de la LCD durante el tono | 14302 µs |
| BASIC, ventana estable de 5,171 s | 259 frames de emulación (50,09/s) y 128 de LCD (24,75/s) |
| Contadores tras la ventana estable | Todos los de incidencias de audio siguen en cero |

El diagnóstico observó también cambios físicos de volumen sin perder audio.
En la ventana de BASIC, el teclado tuvo un máximo de 1015 µs y la CPU no superó
16905 µs por frame. Se conservan extractos de las mediciones en
[`docs/performance-v03.txt`](docs/performance-v03.txt).

Siguen pendientes la carga completa de juegos comerciales, las cargas múltiples,
la cobertura física de todos los atajos y joysticks, y la valoración del sonido
de cada juego. La instalación desde M5Launcher sigue pendiente; en esta tarea
se escribió la partición identificada por USB con copia de respaldo.

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

**V0.3 está compilada e instalada con pruebas de núcleo, ROM, audio concurrente
y rendimiento físico. La compatibilidad y el sonido de cada juego concreto
requieren su propia comprobación.**
