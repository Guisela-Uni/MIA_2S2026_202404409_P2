#ifndef JOURNALING_H
#define JOURNALING_H

#include "ext2_utils.h"
#include <string>
#include <vector>

// Indica si el contexto corresponde a una particion formateada como EXT3.
bool esExt3(const Ext2Context &ctx);

// Registra una operacion en el primer espacio libre del journal de EXT3.
// Devuelve false si el filesystem no es EXT3, el journal esta lleno o falla I/O.
bool registrarJournal(
    const Ext2Context &ctx,
    const std::string &operacion,
    const std::string &path,
    const std::string &contenido = ""
);

// Lee las entradas activas del journal en el orden en que fueron registradas.
std::vector<journaling> leerJournal(const Ext2Context &ctx);

#endif