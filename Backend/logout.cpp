#include "logout.h"
#include <iostream>
void logout(std::vector<std::string>&, EstadoGlobal& e) {
    if (!e.sesion.activa) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    std::cout << "[EXITOSO] Sesion de " << e.sesion.usuario << " cerrada.\n";
    e.sesion = {};
}