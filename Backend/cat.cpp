#include "cat.h"
#include "ext2_utils.h"
#include <algorithm>
#include <cctype>
#include <iostream>
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

}

void cat(std::vector<std::string>& p, EstadoGlobal& e) {
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    bool alguno = false;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v) || k.rfind("file", 0) != 0) {
            std::cout << "[ERROR] CAT requiere parametros -fileN.\n";
            return;
        }
        
        int n;
        if (!resolverRuta(c, v, n)) {
            std::cout << "[ERROR] No existe el archivo: " << v << "\n";
            return;
        }
        
        inodo x{};
        leerInodo(c, n, x);
        
        if (x.i_type != 1) {
            std::cout << "[ERROR] La ruta no corresponde a un archivo: " << v << "\n";
            return;
        }
        
        if (!puedeLeer(x, e.sesion.uid, e.sesion.gid)) {
            std::cout << "[ERROR] No tiene permiso de lectura: " << v << "\n";
            return;
        }
        
        if (alguno) {
            std::cout << "\n";
        }
        
        std::cout << leerArchivo(c, x);
        alguno = true;
    }
    
    if (alguno) {
        std::cout << "\n";
    }
}