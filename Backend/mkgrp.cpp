#include "mkgrp.h"
#include "ext2_utils.h"
#include "journaling.h"
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

} // namespace

void mkgrp(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string name;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v) || k != "name") {
            std::cout << "[ERROR] MKGRP requiere -name=VALOR.\n";
            return;
        }
        
        name = v;
    }
    
    if (name.empty() || name.size() > 10) {
        std::cout << "[ERROR] Nombre de grupo invalido (maximo 10 caracteres).\n";
        return;
    }
    
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    if (e.sesion.uid != 1) {
        std::cout << "[ERROR] Solo el usuario root puede crear grupos.\n";
        return;
    }
    
    auto r = leerUsers(c);
    
    if (gidActivo(r, name) > 0) {
        std::cout << "[ERROR] El grupo ya existe.\n";
        return;
    }
    
    int next = 0;
    
    for (auto& x : r) {
        if (x.type == 'G') {
            next = std::max(next, x.id);
        }
    }
    
    r.push_back({next + 1, 'G', name, {}, {}});
    
    if (!escribirUsers(c, r)) {
        std::cout << "[ERROR] No se pudo actualizar users.txt.\n";
        return;
    }

    if (esExt3(c) && !registrarJournal(c, "mkgrp", "/users.txt", "grupo=" + name)) {
        std::cout << "[AVISO] El grupo se creo, pero no se pudo registrar en el journaling.\n";
    }
    
    std::cout << "[EXITOSO] Grupo '" << name << "' creado.\n";
}