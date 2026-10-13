#include "rmdisk.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <set>

void rmdisk(std::vector<std::string>& parametros, EstadoGlobal& estado) {
    std::string path;
    
    for (size_t i = 1; i < parametros.size(); ++i) {
        auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            std::cout << "[ERROR] RMDISK requiere -path=valor.\n";
            return;
        }
        
        std::string key = parametros[i].substr(0, pos);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        
        if (key != "path") {
            std::cout << "[ERROR] Parametro no reconocido en RMDISK.\n";
            return;
        }
        
        path = parametros[i].substr(pos + 1);
    }
    
    if (path.empty() || !std::filesystem::exists(path)) {
        std::cout << "[ERROR] El disco no existe: " << path << "\n";
        return;
    }
    
    // El archivo se elimina primero. Si falla, se conserva el estado en memoria.
    std::error_code ec;
    const bool eliminado = std::filesystem::remove(path, ec);
    if (ec || !eliminado) {
        std::cout << "[ERROR] No se pudo eliminar el disco";
        if (ec) std::cout << ": " << ec.message();
        std::cout << "\n";
        return;
    }

    std::set<std::string> idsEliminados;
    for (const auto &d : estado.discosMontados) {
        if (d.ruta != path) continue;
        for (const auto &p : d.particiones) {
            if (!p.id.empty()) idsEliminados.insert(p.id);
        }
    }

    estado.discosMontados.erase(
        std::remove_if(
            estado.discosMontados.begin(),
            estado.discosMontados.end(),
            [&](const disco &d) { return d.ruta == path; }
        ),
        estado.discosMontados.end()
    );

    if (estado.sesion.activa && idsEliminados.count(estado.sesion.id)) {
        estado.sesion = EstadoGlobal::Sesion{};
    }

    // Las claves persistentes de formato usan "ruta-del-disco|nombre-particion".
    const std::string prefijo = path + "|";
    for (auto it = estado.particionesFormateadas.begin();
         it != estado.particionesFormateadas.end();) {
        if (it->first.rfind(prefijo, 0) == 0) {
            it = estado.particionesFormateadas.erase(it);
        } else {
            ++it;
        }
    }

    estado.discos.erase(path);
    std::cout << "[EXITOSO] Disco eliminado: " << path << "\n";
}