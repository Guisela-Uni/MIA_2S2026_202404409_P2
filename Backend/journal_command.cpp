#include "journal_command.h"

#include "ext2_utils.h"
#include "journaling.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

std::string minusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return texto;
}

bool separarParametro(const std::string &token, std::string &clave, std::string &valor) {
    const std::size_t posicion = token.find('=');
    if (posicion == std::string::npos) {
        return false;
    }

    clave = minusculas(token.substr(0, posicion));
    valor = token.substr(posicion + 1);
    return true;
}

std::string fechaHora(float marcaTiempo) {
    const std::time_t tiempo = static_cast<std::time_t>(marcaTiempo);
    std::tm *local = std::localtime(&tiempo);
    if (local == nullptr) {
        return "fecha no disponible";
    }

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local);
    return buffer;
}

} // namespace

void mostrarJournal(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string id;

    for (std::size_t i = 1; i < parametros.size(); ++i) {
        std::string clave;
        std::string valor;
        if (!separarParametro(parametros[i], clave, valor)) {
            std::cout << "[ERROR] JOURNALING requiere parametros clave=valor.\n";
            return;
        }

        if (clave == "id") {
            id = valor;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en JOURNALING: -"
                      << clave << ".\n";
            return;
        }
    }

    if (id.empty()) {
        std::cout << "[ERROR] JOURNALING requiere -id.\n";
        return;
    }

    Ext2Context contexto{};
    if (!obtenerContexto(estado, id, contexto)) {
        std::cout << "[ERROR] No se pudo leer el filesystem de la particion "
                  << id << ".\n";
        return;
    }

    if (!esExt3(contexto)) {
        std::cout << "[ERROR] La particion " << id
                  << " no esta formateada como EXT3.\n";
        return;
    }

    const std::vector<journaling> entradas = leerJournal(contexto);
    if (entradas.empty()) {
        std::cout << "[INFO] El journaling de la particion " << id
                  << " no contiene operaciones registradas.\n";
        return;
    }

    std::cout << "JOURNALING - Particion " << id << '\n';
    std::cout << std::left
              << std::setw(10) << "Registro" << " | "
              << std::setw(12) << "Operacion" << " | "
              << std::setw(32) << "Ruta" << " | "
              << std::setw(19) << "Fecha y hora" << " | "
              << "Contenido\n";
    std::cout << std::string(100, '-') << '\n';

    for (const journaling &entrada : entradas) {
        std::cout << std::left
                  << std::setw(10) << entrada.j_count << " | "
                  << std::setw(12) << entrada.j_content.i_operation << " | "
                  << std::setw(32) << entrada.j_content.i_path << " | "
                  << std::setw(19) << fechaHora(entrada.j_content.i_date) << " | "
                  << entrada.j_content.i_content << '\n';
    }
}
