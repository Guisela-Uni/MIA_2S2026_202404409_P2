#ifndef JOURNAL_COMMAND_H
#define JOURNAL_COMMAND_H

#include "estado.h"
#include <string>
#include <vector>

// Muestra las entradas de journaling de una particion EXT3.
void mostrarJournal(std::vector<std::string> &parametros, EstadoGlobal &estado);

#endif
