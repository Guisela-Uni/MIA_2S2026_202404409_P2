#include "rmdisk.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

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
    
    // Un disco no debe quedar montado al eliminarse.
    estado.discosMontados.erase(
        std::remove_if(
            estado.discosMontados.begin(),
            estado.discosMontados.end(),
            [&](const disco& d) { return d.ruta == path; }
        ),
        estado.discosMontados.end()
    );
    
    estado.particionesFormateadas.clear();
    
    std::error_code ec;
    std::filesystem::remove(path, ec);
    
    if (ec) {
        std::cout << "[ERROR] No se pudo eliminar el disco: " << ec.message() << "\n";
        return;
    }
    
    estado.discos.erase(path);
    std::cout << "[EXITOSO] Disco eliminado: " << path << "\n";
}