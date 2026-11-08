#pragma once
#include "Reader.h"
#include "Spectrum.h"

namespace spectrum {
enum class SnapshotKind { Sna,Z80 };
// A caller-owned 48 KiB workspace keeps invalid files from changing the machine.
bool loadSnapshot(Machine &machine,Reader &reader,SnapshotKind kind,
                  uint8_t *workspace,const char *&error);
}
