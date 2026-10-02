#ifndef ESTADO_H
#define ESTADO_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include "structs.h"

struct SimDisk {
    std::string path;
    int tamano = 0;
    char fit = 'f';
    std::time_t creado = 0;
    int signature = 0;
    particion particiones[4]{};
};

struct SimFile {
    int size = 0;
    std::string origen;
};

struct EstadoGlobal {
    // Cache del MBR. La fuente persistente real es el archivo .mia.
    std::map<std::string, SimDisk> discos;

    // MOUNT es deliberadamente RAM-only según el enunciado.
    std::vector<disco> discosMontados;
    std::map<std::string, bool> particionesFormateadas;

    // Compatibilidad con la fase anterior; ya no es la representación principal.
    std::vector<usuario> usuarios;
    std::map<std::string, SimFile> archivos;
    std::set<std::string> carpetas;

    // A, B, C... identifica discos diferentes en el ID de MOUNT.
    char siguienteLetraDisco = 'A';

    struct Sesion { bool activa=false; std::string usuario; std::string id; int uid=-1; int gid=-1; } sesion;
};

#endif