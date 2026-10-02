#include "mkdisk.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool splitParam(const std::string& token, std::string& key, std::string& value) {
    const auto pos = token.find('=');
    if (pos == std::string::npos) {
        return false;
    }
    
    key = lower(token.substr(0, pos));
    value = token.substr(pos + 1);
    return true;
}

} // namespace

void mkdisk(std::vector<std::string>& parametros, EstadoGlobal& estado) {
    int size = -1;
    std::string fit = "ff", unit = "m", path;
    
    for (size_t i = 1; i < parametros.size(); ++i) {
        std::string k, v;
        
        if (!splitParam(parametros[i], k, v)) {
            std::cout << "[ERROR] MKDISK requiere parametros clave=valor.\n";
            return;
        }
        
        if (k == "size") {
            try {
                size = std::stoi(v);
            } catch (...) {
                std::cout << "[ERROR] -size debe ser entero.\n";
                return;
            }
        } else if (k == "fit") {
            fit = lower(v);
        } else if (k == "unit") {
            unit = lower(v);
        } else if (k == "path") {
            path = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MKDISK: -" << k << "\n";
            return;
        }
    }
    
    if (size <= 0 || path.empty()) {
        std::cout << "[ERROR] MKDISK requiere -size>0 y -path.\n";
        return;
    }
    
    char fitc;
    if (fit == "ff") {
        fitc = 'f';
    } else if (fit == "bf") {
        fitc = 'b';
    } else if (fit == "wf") {
        fitc = 'w';
    } else {
        std::cout << "[ERROR] -fit debe ser BF, FF o WF.\n";
        return;
    }
    
    long long bytes = (unit == "k") ? 1LL * size * 1024 : (unit == "m" ? 1LL * size * 1024 * 1024 : -1);
    
    if (bytes <= 0 || bytes > 0x7fffffffLL) {
        std::cout << "[ERROR] -unit debe ser K o M y el tamaño debe ser valido.\n";
        return;
    }
    
    if (std::filesystem::path(path).extension() != ".mia") {
        std::cout << "[ERROR] El disco debe utilizar extension .mia.\n";
        return;
    }
    
    if (std::filesystem::exists(path)) {
        std::cout << "[ERROR] Ya existe el archivo de disco: " << path << "\n";
        return;
    }
    
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    
    if (ec) {
        std::cout << "[ERROR] No se pudieron crear las carpetas de la ruta.\n";
        return;
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cout << "[ERROR] No se pudo crear el archivo .mia.\n";
        return;
    }
    
    const char zeros[1024]{};
    long long restantes = bytes;
    
    while (restantes > 0) {
        const auto n = std::min<long long>(restantes, sizeof(zeros));
        f.write(zeros, n);
        
        if (!f) {
            std::cout << "[ERROR] Fallo escribiendo el disco.\n";
            return;
        }
        
        restantes -= n;
    }
    f.close();

    MBR mbr{};
    mbr.mbr_tamano = int(bytes);
    mbr.mbr_fecha_creacion = std::time(nullptr);
    mbr.dsk_fit = fitc;
    
    std::random_device rd;
    mbr.mbr_dsk_signature = int(rd());
    
    for (auto& p : mbr.mbr_partition) {
        inicializarParticion(p);
    }
    
    if (!escribirMBR(path, mbr)) {
        std::filesystem::remove(path);
        std::cout << "[ERROR] No se pudo escribir el MBR.\n";
        return;
    }

    SimDisk d;
    cargarDisco(path, d);
    estado.discos[path] = d;
    
    std::cout << "[EXITOSO] Disco creado fisicamente: " << path << " (" << bytes << " bytes)\n";
}