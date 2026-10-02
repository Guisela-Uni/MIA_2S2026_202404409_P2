#ifndef FDISK
#define FDISK

#include "structs.h"
#include "estado.h"

#include <iostream>
#include <vector>
#include <algorithm>

// recibe parametros que van algo tipo:
//parametros[0] = "fdisk"
//parametros[1] = "size=10"
//parametros[2] = "path=/home/user/disco.dk"
void fdisk(std::vector<std::string> &parametros, EstadoGlobal &estado);

// CREA UNA NUEVA PARTICION- SIZE, TIPO (primaria, logica, ex), PATH del disco,nombre de la particion, fit (best, worst, first).
// Trabaja unicamente sobre el SimDisk en memoria (estado.discos[ruta]).
void particionar(int size, char tipo, const std::string &ruta, const std::string &nombre, char fit, SimDisk &disco);

#endif
