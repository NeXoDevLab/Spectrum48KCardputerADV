# Créditos y procedencia

## CPU Z80

[superzazu/z80](https://github.com/superzazu/z80), Nicolas Allemand (2019), MIT.

Commit fijado: `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`.
Código en `src/vendor/z80.c` y `src/vendor/z80.h`; licencia íntegra en
`src/vendor/LICENSE` y procedencia en `src/vendor/UPSTREAM.txt`.

El port modifica las firmas de los callbacks de entrada/salida para recibir
la dirección completa de 16 bits. IN/OUT inmediatos usan el acumulador como
byte alto; las instrucciones con `(C)` usan BC; OUTI/OUTD usan B decrementado.
Los archivos modificados llevan un comentario de identificación. No se utiliza
el núcleo CPC de Tom Walker en este proyecto de Spectrum.
En V0.3 se añade una directiva GCC O3 para el intérprete al compilar para ESP32;
no cambia las instrucciones emuladas. La procedencia permanece en UPSTREAM.txt.

La memoria, ULA, teclado, cassette, formatos de instantánea y adaptación al
Cardputer se han escrito para este proyecto. La integración de hardware utiliza
como referencia el proyecto Amstrad del mismo espacio de trabajo.
La organización de carpetas adapta su política de listado acotado, navegación
y movimiento sin sobrescritura, con cuatro formatos propios de Spectrum.

## Dependencias Arduino

M5Cardputer, M5Unified y M5GFX de M5Stack, y el core ESP32 de M5Stack/Espressif,
conservan las licencias incluidas en sus distribuciones. No se vendorizan aquí.

## Referencias técnicas

- [Documentación del Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv).
- [Formato Z80](https://worldofspectrum.org/faq/reference/z80format.htm).
- [Formatos SNA y TAP](https://worldofspectrum.org/faq/reference/formats.htm).
- [Especificación TZX](https://worldofspectrum.net/TZXformat.html).
- [Análisis de LD-BYTES de la ROM](https://skoolkid.github.io/rom/asm/0556.html).

Las referencias documentan el comportamiento. No se distribuye la ROM del
Spectrum, sus gráficos ni juegos. Las pruebas generan datos y programas
sintéticos. La prueba opcional `tests/rom_tap.cpp` lee una ROM externa que
aporta quien ejecuta las pruebas; no la copia al proyecto ni al firmware.
