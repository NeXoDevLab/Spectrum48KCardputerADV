# Capturas y vídeos de Spectrum V0.3

Las capturas proceden del Cardputer ADV conectado, ejecutando Oh Mummy y
Phantomas 2 desde los archivos TAP aportados en su microSD. No se distribuyen
las cintas ni las ROMs. Los derechos de cada juego corresponden a sus autores.

Se utilizó una compilación temporal con `ZX_USB_RECORDING`; al terminar se
restauró el firmware normal. Imagen y PCM se capturan de la misma ejecución,
con secuencias y posiciones de muestras verificadas. Cada frame representa
69888 T-states a 3,5 MHz; el vídeo recibe una imagen cada dos frames. La duración
publicada sigue el tiempo emulado, aunque la transferencia USB sea más lenta.
Se conserva toda la carga y una cola de 20 segundos después del fin de cinta.

Los PNG de carga y menú se guardan del flujo RGB565 decodificado, a 240×135.
BASIC se obtuvo con el comando USB de captura. Los MP4 se colocan en la foto
proporcionada anteriormente para la galería de Amstrad: la LCD mantiene sus
240×135 píxeles sobre un lienzo de 880×580. Es una composición documental,
no una filmación externa del dispositivo.

El audio de la emulación se convierte a AAC con filtro de 20 Hz, ganancia 1,8
y limitador; no se añade música ni sonido externo. Al componer el marco se
copia el AAC sin recodificar y se verifica su hash. Los JSON asociados contienen
firmware, duración, protocolo y hashes de vídeo, fotografía y audio.

Fotografía del Cardputer ADV: M5Stack, misma referencia facilitada para el
proyecto Amstrad. Fuente: https://docs.m5stack.com/en/core/Cardputer-Adv

## Reproducir la captura

Compilar temporalmente con `compiler.cpp.extra_flags=-DZX_USB_RECORDING`,
instalar solo en la partición verificada del emulador y ejecutar:

```sh
python3 scripts/record-device.py PUERTO '/SPECTRUM/GAMES/TAP/juego.tap' salida.mp4
```

El script necesita ffmpeg o imageio-ffmpeg; utiliza únicamente la biblioteca
estándar de Python para el puerto serie y la decodificación RGB565. El modo
especial conserva el reloj de la máquina y transmite las muestras por USB;
no representa una medición del rendimiento de audio del firmware normal.
