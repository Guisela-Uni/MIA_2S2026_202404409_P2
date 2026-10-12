#include "rename.h"

#include "ext2_utils.h"
#include "journaling.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
}

void rename_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path, nuevoNombre;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            std::cout << "[ERROR] RENAME requiere parametros -path y -name.\n";
            return;
        }
        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "name") nuevoNombre = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en RENAME: -" << clave << "\n";
            return;
        }
    }
    if (path.empty() || nuevoNombre.empty()) {
        std::cout << "[ERROR] RENAME requiere -path y -name.\n";
        return;
    }
    if (nuevoNombre.size() > 11 || nuevoNombre == "." || nuevoNombre == ".." ||
        nuevoNombre.find('/') != std::string::npos) {
        std::cout << "[ERROR] El nombre debe tener entre 1 y 11 caracteres y no puede ser '.' o '..'.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    const std::string ruta = normalizarRuta(path);
    int inoNum = -1, padre = -1;
    std::string nombreActual;
    if (ruta == "/" || !resolverRuta(ctx, ruta, inoNum, &padre, &nombreActual) || padre < 0) {
        std::cout << "[ERROR] La ruta no existe o no se puede renombrar la raiz.\n";
        return;
    }

    inodo objetivo{};
    if (!leerInodo(ctx, inoNum, objetivo) ||
        !puedeEscribir(objetivo, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de escritura sobre el archivo o carpeta.\n";
        return;
    }
    int existente = -1;
    if (buscarEntrada(ctx, padre, nuevoNombre, existente) && nuevoNombre != nombreActual) {
        std::cout << "[ERROR] Ya existe un elemento con el nombre " << nuevoNombre << ".\n";
        return;
    }
    if (nuevoNombre == nombreActual) {
        std::cout << "[INFO] El nombre no cambia; no se realizo ninguna modificacion.\n";
        return;
    }
    if (!cambiarNombreEntrada(ctx, padre, nombreActual, nuevoNombre)) {
        std::cout << "[ERROR] No se pudo actualizar la entrada del directorio.\n";
        return;
    }

    const auto pos = ruta.find_last_of('/');
    const std::string nuevaRuta = (pos == 0) ? "/" + nuevoNombre : ruta.substr(0, pos + 1) + nuevoNombre;
    if (esExt3(ctx) && !registrarJournal(ctx, "rename", ruta, "name=" + nuevoNombre)) {
        std::cout << "[AVISO] Se renombro el elemento, pero no se pudo registrar en journaling.\n";
    }
    std::cout << "[EXITOSO] Elemento renombrado: " << ruta << " -> " << nuevaRuta << "\n";
}
