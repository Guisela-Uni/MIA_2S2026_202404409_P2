#include "mounted.h"
#include <iostream>
void mounted(std::vector<std::string>& parametros, EstadoGlobal& estado) {
    if (parametros.size() != 1) {
        std::cout << "[ERROR] MOUNTED no recibe parametros.\n";
        return;
    }
    
    bool alguno = false;
    
    for (const auto& d : estado.discosMontados) {
        for (const auto& p : d.particiones) {
            alguno = true;
            std::cout << p.id << " - " << d.nombre << " - " << p.nombre << "\n";
        }
    }
    
    if (!alguno) {
        std::cout << "[INFO] No hay particiones montadas en RAM.\n";
    }
}
