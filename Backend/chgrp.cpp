#include "chgrp.h"
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

} // namespace

void chgrp(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string user, grp;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v)) {
            std::cout << "[ERROR] CHGRP requiere parametros clave=valor.\n";
            return;
        }
        
        if (k == "user") {
            user = v;
        } else if (k == "grp") {
            grp = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en CHGRP.\n";
            return;
        }
    }
    
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    if (e.sesion.uid != 1) {
        std::cout << "[ERROR] Solo root puede cambiar grupos.\n";
        return;
    }
    
    auto r = leerUsers(c);
    
    if (gidActivo(r, grp) < 0) {
        std::cout << "[ERROR] El grupo no existe o esta eliminado.\n";
        return;
    }
    
    bool ok = false;
    
    for (auto& x : r) {
        if (x.type == 'U' && x.id > 0 && x.user == user) {
            x.group = grp;
            ok = true;
        }
    }
    
    if (!ok) {
        std::cout << "[ERROR] El usuario no existe.\n";
        return;
    }
    
    if (!escribirUsers(c, r)) {
        std::cout << "[ERROR] No se pudo actualizar users.txt.\n";
        return;
    }
    
    std::cout << "[EXITOSO] Grupo del usuario '" << user << "' cambiado a '" << grp << "'.\n";
}