#ifndef MKFILE
#define MKFILE

#include "structs.h"
#include "estado.h"
#include <string>
#include <vector>

// -path (obligatorio), -r (opcional, flag sin valor), -size (opcional),
void mkfile(std::vector<std::string> &parametros, EstadoGlobal &estado);

#endif
