#ifndef DISK_UTILS_H
#define DISK_UTILS_H

#include "estado.h"
#include <string>
#include <vector>

bool existeArchivoDisco(const std::string& path);
bool leerMBR(const std::string& path, MBR& mbr);
bool escribirMBR(const std::string& path, const MBR& mbr);
bool cargarDisco(const std::string& path, SimDisk& disco);
void inicializarParticion(particion& p);
void limpiarParticion(particion& p);
std::string nombreDisco(const std::string& path);

bool buscarParticionPrimaria(const SimDisk& disco, const std::string& nombre, int& indice);
bool buscarExtendida(const SimDisk& disco, int& indice);
bool buscarLogica(const std::string& path, const SimDisk& disco, const std::string& nombre,
                  int& posEBR, EBR& encontrada);

bool leerEBR(const std::string& path, int offset, EBR& ebr);
bool escribirEBR(const std::string& path, int offset, const EBR& ebr);

bool buscarMontada(const EstadoGlobal& estado, const std::string& id,
                   const disco*& discoMontado, const montada*& particionMontada);
bool buscarMontada(EstadoGlobal& estado, const std::string& id,
                   disco*& discoMontado, montada*& particionMontada);

#endif
