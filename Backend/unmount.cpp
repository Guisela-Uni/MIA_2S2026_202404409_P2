#include "unmount.h"

#include "disk_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
}

void unmount_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string id;

    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos || lower(parametros[i].substr(0, pos)) != "id") {
            std::cout << "[ERROR] UNMOUNT solo acepta -id=VALOR.\n";
            return;
        }
        id = parametros[i].substr(pos + 1);
    }

    if (id.empty()) {
        std::cout << "[ERROR] UNMOUNT requiere -id.\n";
        return;
    }

    for (auto diskIt = estado.discosMontados.begin(); diskIt != estado.discosMontados.end(); ++diskIt) {
        auto partIt = std::find_if(diskIt->particiones.begin(), diskIt->particiones.end(),
                                   [&](const montada &particion) { return particion.id == id; });
        if (partIt == diskIt->particiones.end()) continue;

        const std::string rutaDisco = diskIt->ruta;
        const int indiceMBR = partIt->posMBR;
        const int offsetEBR = partIt->posEBR;
        const std::string idSesion = estado.sesion.id;

        if (indiceMBR >= 0) {
            MBR mbr{};
            if (!leerMBR(rutaDisco, mbr)) {
                std::cout << "[ERROR] No se pudo leer el MBR para desmontar la particion.\n";
                return;
            }
            mbr.mbr_partition[indiceMBR].part_status = '0';
            mbr.mbr_partition[indiceMBR].part_correlativ = 0;
            std::memset(mbr.mbr_partition[indiceMBR].part_id, 0,
                        sizeof(mbr.mbr_partition[indiceMBR].part_id));
            if (!escribirMBR(rutaDisco, mbr)) {
                std::cout << "[ERROR] No se pudo actualizar el MBR al desmontar.\n";
                return;
            }
        } else if (offsetEBR >= 0) {
            EBR ebr{};
            if (!leerEBR(rutaDisco, offsetEBR, ebr)) {
                std::cout << "[ERROR] No se pudo leer el EBR al desmontar la particion logica.\n";
                return;
            }
            ebr.part_status = '0';
            if (!escribirEBR(rutaDisco, offsetEBR, ebr)) {
                std::cout << "[ERROR] No se pudo actualizar el EBR al desmontar.\n";
                return;
            }
        }

        if (estado.sesion.activa && idSesion == id) {
            estado.sesion = {};
        }
        diskIt->particiones.erase(partIt);
        if (diskIt->particiones.empty()) estado.discosMontados.erase(diskIt);
        std::cout << "[EXITOSO] Particion " << id << " desmontada.\n";
        return;
    }

    std::cout << "[ERROR] No existe una particion montada con ID " << id << ".\n";
}
