#include "login.h"
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

void login(std::vector<std::string>& p, EstadoGlobal& e) {
    if (e.sesion.activa) {
        std::cout << "[ERROR] Ya existe una sesion activa. Debe ejecutar LOGOUT primero.\n";
        return;
    }
    
    std::string user, pass, id;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (!split(p[i], k, v)) {
            std::cout << "[ERROR] LOGIN requiere parametros clave=valor.\n";
            return;
        }
        
        if (k == "user") {
            user = v;
        } else if (k == "pass") {
            pass = v;
        } else if (k == "id") {
            id = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en LOGIN: -" << k << "\n";
            return;
        }
    }
    
    if (user.empty() || pass.empty() || id.empty()) {
        std::cout << "[ERROR] LOGIN requiere -user, -pass e -id.\n";
        return;
    }
    
    Ext2Context c;
    
    if (!obtenerContexto(e, id, c)) {
        std::cout << "[ERROR] La particion indicada no esta montada o no esta formateada.\n";
        return;
    }
    
    auto r = leerUsers(c);
    int uid = uidActivo(r, user, pass);
    
    if (uid < 0) {
        bool existe = false;
        for (auto& x : r) {
            if (x.id > 0 && x.type == 'U' && x.user == user) {
                existe = true;
            }
        }
        
        if (existe) {
            std::cout << "[ERROR] Autenticacion fallida.\n";
        } else {
            std::cout << "[ERROR] El usuario no existe.\n";
        }
        return;
    }
    
    std::string g = grupoUsuario(r, user);
    int gid = gidActivo(r, g);
    
    if (gid < 0) {
        std::cout << "[ERROR] El grupo del usuario no existe.\n";
        return;
    }
    
    e.sesion = {true, user, id, uid, gid};
    std::cout << "[EXITOSO] Sesion iniciada como " << user << " en " << id << ".\n";
}