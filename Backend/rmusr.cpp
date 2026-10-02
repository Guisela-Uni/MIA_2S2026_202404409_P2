#include "rmusr.h"
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
    if (p == std::string::npos) return false;
    k = low(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

} // namespace

void rmusr(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string user;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        if (!split(p[i], k, v) || k != "user") {
            std::cout << "[ERROR] RMUSR requiere -user=VALOR.\n";
            return;
        }
        user = v;
    }

    Ext2Context c;
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }

    if (e.sesion.uid != 1) {
        std::cout << "[ERROR] Solo root puede eliminar usuarios.\n";
        return;
    }

    auto r = leerUsers(c);
    bool ok = false;

    for (auto& x : r) {
        if (x.type == 'U' && x.id > 0 && x.user == user) {
            x.id = 0;
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

    std::cout << "[EXITOSO] Usuario '" << user << "' eliminado.\n";
}