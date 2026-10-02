#include "mount.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <regex>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool split(const std::string& t, std::string& k, std::string& v) {
    auto p = t.find('=');
    if (p == std::string::npos) {
        return false;
    }
    
    k = lower(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

// Carnet del estudiante: se utilizan los dos ultimos digitos solicitados por el proyecto.
// Para este proyecto los IDs comienzan con 09.
std::string carnetBase() {
    return "09";
}

} // namespace

void mount(std::vector<std::string>& parametros, EstadoGlobal& estado) {
    std::string path, name;
    
    for (size_t i = 1; i < parametros.size(); ++i) {
        std::string k, v;
        
        if (!split(parametros[i], k, v)) {
            std::cout << "[ERROR] MOUNT requiere clave=valor.\n";
            return;
        }
        
        if (k == "path") {
            path = v;
        } else if (k == "name") {
            name = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MOUNT: -" << k << "\n";
            return;
        }
    }
    
    if (path.empty() || name.empty()) {
        std::cout << "[ERROR] MOUNT requiere -path y -name.\n";
        return;
    }
    
    SimDisk d;
    if (!cargarDisco(path, d)) {
        std::cout << "[ERROR] El archivo .mia no existe o no contiene un MBR valido.\n";
        return;
    }
    
    int idx = -1;
    if (buscarParticionPrimaria(d, name, idx)) {
        // primaria
    } else {
        int ext = -1;
        if (buscarExtendida(d, ext)) {
            int pos = -1;
            EBR e{};
            
            if (buscarLogica(path, d, name, pos, e)) {
                std::cout << "[INFO] Particion logica encontrada; el enunciado reserva su uso para MOUNT/REPORTES.\n";
                /* soportada */
            } else {
                std::cout << "[ERROR] La particion '" << name << "' no existe.\n";
                return;
            }
            // pos se conserva para el montaje
        } else {
            std::cout << "[ERROR] La particion '" << name << "' no existe.\n";
            return;
        }
    }
    
    std::string diskName = nombreDisco(path);
    disco* entrada = nullptr;
    
    for (auto& x : estado.discosMontados) {
        if (x.ruta == path) {
            entrada = &x;
            break;
        }
    }
    
    if (!entrada) {
        estado.discosMontados.push_back({});
        entrada = &estado.discosMontados.back();
        entrada->ruta = path;
        entrada->nombre = diskName;
        entrada->letra = estado.siguienteLetraDisco++;
        entrada->contador = 1;
    }
    
    for (const auto& p : entrada->particiones) {
        if (p.nombre == name) {
            std::cout << "[ERROR] La particion ya esta montada con ID " << p.id << ".\n";
            return;
        }
    }
    
    montada m;
    m.nombre = name;
    m.ruta = path;
    m.posMBR = -1;
    m.posEBR = -1;
    m.size = 0;
    
    if (idx >= 0) {
        m.posMBR = idx;
        m.size = d.particiones[idx].part_s;
    } else {
        int e = -1;
        buscarExtendida(d, e);
        int pos = -1;
        EBR ebr{};
        buscarLogica(path, d, name, pos, ebr);
        m.posEBR = pos;
        m.size = ebr.part_s;
    }
    
    const std::string id = carnetBase() + std::to_string(entrada->contador) + std::string(1, entrada->letra);
    m.id = id;
    entrada->contador++;

    if (m.posMBR >= 0) {
        d.particiones[m.posMBR].part_status = '1';
        d.particiones[m.posMBR].part_correlativ = entrada->contador - 1;
        std::memcpy(
            d.particiones[m.posMBR].part_id,
            id.data(),
            std::min<size_t>(sizeof(d.particiones[m.posMBR].part_id), id.size())
        );
    }
    
    estado.discos[path] = d;
    entrada->particiones.push_back(m);
    
    std::cout << "[EXITOSO] Particion montada.\nID: " << id << "\n";
}