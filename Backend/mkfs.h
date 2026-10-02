#ifndef MKFS
#define MKFS

#include "structs.h"
#include "estado.h"
#include <string>
#include <vector>

// parametros[0] = "mkfs" id (obligatorio): id generado por MOUNT type (opcional): "full" (unico valor valido). Default: full.
void mkfs(std::vector<std::string> &parametros, EstadoGlobal &estado);

#endif
