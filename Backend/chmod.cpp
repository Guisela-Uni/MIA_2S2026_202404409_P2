#include "chmod.h"

#include "ext2_utils.h"
#include "journaling.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <set>

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool analizarPermisos(const std::string &texto, int &permisos) {
    if (texto.size() != 3) return false;

    for (unsigned char c : texto) {
        if (c < '0' || c > '7') return false;
    }

    permisos = (texto[0] - '0') * 100 + (texto[1] - '0') * 10 + (texto[2] - '0');
    return true;
}

struct ResultadoChmod {
    int modificados = 0;
    int omitidos = 0;
};

void aplicarRecursivo(Ext2Context &ctx, int inoNum, int uidActual, int gidActual,
                      bool esRoot, int permisos, std::set<int> &visitados,
                      ResultadoChmod &resultado) {
    if (!visitados.insert(inoNum).second) return;

    inodo actual{};
    if (!leerInodo(ctx, inoNum, actual)) {
        ++resultado.omitidos;
        return;
    }

    const bool autorizado = esRoot || actual.i_uid == uidActual;
    const bool puedeRecorrer = actual.i_type == 0 &&
        puedeLeer(actual, uidActual, gidActual);

    if (autorizado) {
        if (actual.i_perm != permisos) {
            actual.i_perm = permisos;
            actual.i_ctime = std::time(nullptr);
            if (escribirInodo(ctx, inoNum, actual)) ++resultado.modificados;
            else ++resultado.omitidos;
        }
    } else {
        ++resultado.omitidos;
    }

    if (actual.i_type != 0) return;
    if (!esRoot && !puedeRecorrer) {
        ++resultado.omitidos;
        return;
    }

    std::vector<std::pair<std::string, int>> entradas;
    if (!listarEntradasDirectorio(ctx, inoNum, entradas)) {
        ++resultado.omitidos;
        return;
    }

    for (const auto &entrada : entradas) {
        if (entrada.first == "." || entrada.first == "..") continue;
        aplicarRecursivo(ctx, entrada.second, uidActual, gidActual, esRoot,
                         permisos, visitados, resultado);
    }
}

} // namespace

void chmod_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path;
    std::string textoPermisos;
    bool recursivo = false;

    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            if (lower(parametros[i]) == "r") {
                recursivo = true;
                continue;
            }
            std::cout << "[ERROR] CHMOD acepta -path, -ugo y la bandera opcional -r.\n";
            return;
        }

        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "ugo" || clave == "perm") textoPermisos = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en CHMOD: -" << clave << "\n";
            return;
        }
    }

    if (path.empty() || textoPermisos.empty()) {
        std::cout << "[ERROR] CHMOD requiere -path y -ugo=XYZ (por ejemplo, -ugo=640).\n";
        return;
    }

    int permisos = 0;
    if (!analizarPermisos(textoPermisos, permisos)) {
        std::cout << "[ERROR] Los permisos deben ser tres digitos octales entre 0 y 7, por ejemplo 640.\n";
        return;
    }

    Ext2Context ctx{};
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }

    const std::string ruta = normalizarRuta(path);
    int inoNum = -1;
    if (!resolverRuta(ctx, ruta, inoNum)) {
        std::cout << "[ERROR] No existe la ruta: " << ruta << "\n";
        return;
    }

    inodo objetivo{};
    if (!leerInodo(ctx, inoNum, objetivo)) {
        std::cout << "[ERROR] No se pudo leer el elemento indicado.\n";
        return;
    }

    const bool esRoot = estado.sesion.uid == 1;
    if (!esRoot && objetivo.i_uid != estado.sesion.uid) {
        std::cout << "[ERROR] Solo root o el propietario pueden cambiar los permisos del elemento.\n";
        return;
    }

    ResultadoChmod resultado;
    if (recursivo) {
        std::set<int> visitados;
        aplicarRecursivo(ctx, inoNum, estado.sesion.uid, estado.sesion.gid,
                         esRoot, permisos, visitados, resultado);
    } else if (objetivo.i_perm != permisos) {
        objetivo.i_perm = permisos;
        objetivo.i_ctime = std::time(nullptr);
        if (escribirInodo(ctx, inoNum, objetivo)) ++resultado.modificados;
        else ++resultado.omitidos;
    }

    if (resultado.modificados > 0 && esExt3(ctx) &&
        !registrarJournal(ctx, "chmod", ruta,
                          "ugo=" + textoPermisos + (recursivo ? ",r=true" : ""))) {
        std::cout << "[AVISO] Los permisos cambiaron, pero no se pudo registrar CHMOD en journaling.\n";
    }

    if (resultado.modificados == 0 && resultado.omitidos == 0) {
        std::cout << "[INFO] Los permisos ya tenian el valor solicitado; no se hicieron cambios.\n";
        return;
    }
    if (resultado.modificados == 0) {
        std::cout << "[ERROR] No se modificaron permisos; compruebe la propiedad y los permisos de acceso.\n";
        return;
    }

    std::cout << "[EXITOSO] Permisos actualizados a " << textoPermisos << " en "
              << resultado.modificados << " elemento(s).\n";
    if (resultado.omitidos > 0) {
        std::cout << "[AVISO] Se omitieron " << resultado.omitidos
                  << " elemento(s) por permisos o errores de lectura/escritura.\n";
    }
}
