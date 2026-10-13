#include "mkusr.h"
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

void mkusr(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string user, pass, grp;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v)) {
            std::cout << "[ERROR] MKUSR requiere clave=valor.\n";
            return;
        }
        
        if (k == "user") {
            user = v;
        } else if (k == "pass") {
            pass = v;
        } else if (k == "grp") {
            grp = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MKUSR.\n";
            return;
        }
    }
    
    if (user.empty() || pass.empty() || grp.empty() || 
        user.size() > 10 || pass.size() > 10 || grp.size() > 10) {
        std::cout << "[ERROR] MKUSR requiere user/pass/grp y maximo 10 caracteres cada uno.\n";
        return;
    }
    
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    if (e.sesion.uid != 1) {
        std::cout << "[ERROR] Solo root puede crear usuarios.\n";
        return;
    }
    
    auto r = leerUsers(c);
    
    if (gidActivo(r, grp) < 0) {
        std::cout << "[ERROR] El grupo no existe o esta eliminado.\n";
        return;
    }
    
    for (auto& x : r) {
        if (x.type == 'U' && x.id > 0 && x.user == user) {
            std::cout << "[ERROR] El usuario ya existe.\n";
            return;
        }
    }
    
    int next = 0;
    
    for (auto& x : r) {
        if (x.type == 'U') {
            next = std::max(next, x.id);
        }
    }
    
    r.push_back({next + 1, 'U', grp, user, pass});
    
    if (!escribirUsers(c, r)) {
        std::cout << "[ERROR] No se pudo actualizar users.txt.\n";
        return;
    }

    if (esExt3(c) && !registrarJournal(c, "mkusr", "/users.txt", "usuario=" + user + ", grupo=" + grp)) {
        std::cout << "[AVISO] El usuario se creo, pero no se pudo registrar en el journaling.\n";
    }
    
    std::cout << "[EXITOSO] Usuario '" << user << "' creado.\n";
}