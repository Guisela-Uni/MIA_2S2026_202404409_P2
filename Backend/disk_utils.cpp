#include "disk_utils.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

bool existeArchivoDisco(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool leerMBR(const std::string& path, MBR& mbr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    return f.good() || f.gcount() == static_cast<std::streamsize>(sizeof(MBR));
}

bool escribirMBR(const std::string& path, const MBR& mbr) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    f.seekp(0);
    f.write(reinterpret_cast<const char*>(&mbr), sizeof(MBR));
    f.flush();
    return static_cast<bool>(f);
}

void inicializarParticion(particion& p) {
    std::memset(&p, 0, sizeof(particion));
    p.part_status = '0';
    p.part_type = '0';
    p.part_fit = 'f';
    p.part_start = -1;
    p.part_s = 0;
    p.part_correlativ = -1;
}

void limpiarParticion(particion& p) { inicializarParticion(p); }

bool cargarDisco(const std::string& path, SimDisk& disco) {
    MBR mbr{};
    if (!leerMBR(path, mbr)) return false;
    disco.path = path;
    disco.tamano = mbr.mbr_tamano;
    disco.fit = mbr.dsk_fit;
    disco.creado = mbr.mbr_fecha_creacion;
    disco.signature = mbr.mbr_dsk_signature;
    for (int i = 0; i < 4; ++i) disco.particiones[i] = mbr.mbr_partition[i];
    return true;
}

std::string nombreDisco(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

bool buscarParticionPrimaria(const SimDisk& disco, const std::string& nombre, int& indice) {
    for (int i = 0; i < 4; ++i) {
        if (disco.particiones[i].part_type == 'p' && std::string(disco.particiones[i].part_name) == nombre) {
            indice = i; return true;
        }
    }
    return false;
}

bool buscarExtendida(const SimDisk& disco, int& indice) {
    for (int i = 0; i < 4; ++i) {
        if (disco.particiones[i].part_type == 'e') { indice = i; return true; }
    }
    return false;
}

bool leerEBR(const std::string& path, int offset, EBR& ebr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(offset);
    f.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
    return static_cast<bool>(f);
}

bool escribirEBR(const std::string& path, int offset, const EBR& ebr) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    f.seekp(offset);
    f.write(reinterpret_cast<const char*>(&ebr), sizeof(EBR));
    f.flush();
    return static_cast<bool>(f);
}

bool buscarLogica(const std::string& path, const SimDisk& disco, const std::string& nombre,
                  int& posEBR, EBR& encontrada) {
    int ext = -1;
    if (!buscarExtendida(disco, ext)) return false;
    const particion& p = disco.particiones[ext];
    int offset = p.part_start;
    const int fin = p.part_start + p.part_s;

    while (offset >= p.part_start && offset + static_cast<int>(sizeof(EBR)) <= fin) {
        EBR e{};
        if (!leerEBR(path, offset, e)) return false;
        if (e.part_s > 0 && std::string(e.part_name) == nombre) {
            posEBR = offset; encontrada = e; return true;
        }
        if (e.part_next == -1 || e.part_next <= offset || e.part_next >= fin) break;
        offset = e.part_next;
    }
    return false;
}

bool buscarMontada(const EstadoGlobal& estado, const std::string& id,
                   const disco*& discoMontado, const montada*& particionMontada) {
    for (const auto& d : estado.discosMontados) {
        for (const auto& p : d.particiones) {
            if (p.id == id) { discoMontado = &d; particionMontada = &p; return true; }
        }
    }
    return false;
}

bool buscarMontada(EstadoGlobal& estado, const std::string& id,
                   disco*& discoMontado, montada*& particionMontada) {
    for (auto& d : estado.discosMontados) {
        for (auto& p : d.particiones) {
            if (p.id == id) { discoMontado = &d; particionMontada = &p; return true; }
        }
    }
    return false;
}