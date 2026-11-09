#!/usr/bin/env python3
"""Package the current firmware and the empty Spectrum SD folders; no ROMs/games."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parent.parent
firmware = root / "firmware" / "Spectrum48K-CardputerADV-V0.3-app.bin"
output = root / "firmware" / "Spectrum48K-CardputerADV-V0.3-SD.zip"
instructions = """Spectrum 48K Cardputer ADV V0.3

1. Extrae este ZIP en la raiz de tu microSD FAT32.
2. Copia tu ROM de 16384 bytes como /SPECTRUM/48.ROM.
3. Copia los juegos en /SPECTRUM/GAMES/TAP, TZX, SNA o Z80.
4. Instala Spectrum48K-CardputerADV-V0.3-app.bin desde M5Launcher.

No se incluyen ROMs ni juegos. No borres otras carpetas de tu tarjeta.
Al arrancar se ordenan tambien los archivos sueltos de GAMES y los de la
antigua carpeta SNAPSHOTS; nunca se sobrescriben archivos existentes.
V0.3 mejora la continuidad del audio con una tarea y una cola independientes,
acelera el filtro de pantalla y mantiene la correccion de carga de V0.2.
"""
with ZipFile(output, "w", ZIP_DEFLATED) as archive:
    for folder in ("SPECTRUM/", "SPECTRUM/GAMES/", "SPECTRUM/GAMES/TAP/",
                   "SPECTRUM/GAMES/TZX/", "SPECTRUM/GAMES/SNA/", "SPECTRUM/GAMES/Z80/"):
        archive.writestr(folder, b"")
    archive.write(firmware, firmware.name)
    archive.write(root / "firmware" / "SHA256SUMS", "SHA256SUMS")
    archive.writestr("LEEME-SPECTRUM.txt", instructions.encode("utf-8"))
print(output)
