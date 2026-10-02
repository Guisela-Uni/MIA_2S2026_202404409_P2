#include "mkdir.h"
#include "ext2_utils.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
namespace {

std::string low(std::string s) {
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
    
    k = low(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

} // namespace

void mkdir_cmd(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string path;
    bool par = false;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v)) {
            if (low(p[i]) == "p") {
                par = true;
                continue;
            }
            std::cout << "[ERROR] MKDIR: parametro invalido " << p[i] << "\n";
            return;
        }
        
        if (k == "path") {
            path = v;
        } else if (k == "p") {
            std::cout << "[ERROR] -p no recibe valor.\n";
            return;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MKDIR: -" << k << "\n";
            return;
        }
    }
    
    if (path.empty()) {
        std::cout << "[ERROR] MKDIR requiere -path.\n";
        return;
    }
    
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    auto parts = partesRuta(path);
    
    if (parts.empty()) {
        std::cout << "[ERROR] La ruta no puede ser /.\n";
        return;
    }
    
    int cur = 0;
    
    for (size_t i = 0; i < parts.size(); i++) {
        int child;
        
        if (buscarEntrada(c, cur, parts[i], child)) {
            inodo x{};
            leerInodo(c, child, x);
            
            if (x.i_type != 0) {
                std::cout << "[ERROR] Ya existe un archivo llamado " << parts[i] << ".\n";
                return;
            }
            
            cur = child;
            continue;
        }
        
        if (!par && i + 1 < parts.size()) {
            std::cout << "[ERROR] No existe la carpeta padre. Use -p.\n";
            return;
        }
        
        inodo parent{};
        
        if (!leerInodo(c, cur, parent) || !puedeEscribir(parent, e.sesion.uid, e.sesion.gid)) {
            std::cout << "[ERROR] No tiene permiso de escritura en la carpeta padre.\n";
            return;
        }
        
        int n;
        
        if (!crearDirectorio(c, cur, parts[i], e.sesion.uid, e.sesion.gid, n)) {
            std::cout << "[ERROR] No se pudo crear la carpeta (sin espacio o nombre demasiado largo).\n";
            return;
        }
        
        cur = n;
    }
    
    std::cout << "[EXITOSO] Directorio creado: " << normalizarRuta(path) << "\n";
}