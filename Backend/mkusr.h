#ifndef MKUSR
#define MKUSR

#include "structs.h"
#include "estado.h"
#include <string>
#include <vector>

// -user, -pass, -grp (todos obligatorios, maximo 10 caracteres cada uno)
void mkusr(std::vector<std::string> &parametros, EstadoGlobal &estado);

#endif
