#include "rmgrp.h"
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
    if (p == std::string::npos) return false;
    k = low(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

} // namespace

void rmgrp(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string name;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        if (!split(p[i], k, v) || k != "name") {
            std::cout << "[ERROR] RMGRP requiere -name=VALOR.\n";
            return;
        }
        name = v;
    }

    Ext2Context c;
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }

    if (e.sesion.uid != 1) {
        std::cout << "[ERROR] Solo root puede eliminar grupos.\n";
        return;
    }

    auto r = leerUsers(c);
    bool ok = false;

    for (auto& x : r) {
        if (x.type == 'G' && x.id > 0 && x.group == name) {
            x.id = 0;
            ok = true;
        }
    }

    if (!ok) {
        std::cout << "[ERROR] El grupo no existe.\n";
        return;
    }

    if (!escribirUsers(c, r)) {
        std::cout << "[ERROR] No se pudo actualizar users.txt.\n";
        return;
    }

    if (esExt3(c) && !registrarJournal(c, "rmgrp", "/users.txt", "grupo=" + name)) {
        std::cout << "[AVISO] El grupo se elimino, pero no se pudo registrar en el journaling.\n";
    }

    std::cout << "[EXITOSO] Grupo '" << name << "' eliminado.\n";
}