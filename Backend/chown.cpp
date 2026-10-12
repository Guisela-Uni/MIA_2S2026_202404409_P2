#include "chown.h"

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

struct ResultadoChown {
    int modificados = 0;
    int omitidos = 0;
};

void cambiarPropietarioRecursivo(Ext2Context &ctx, int inoNum, int uidActual,
                                  int gidActual, int uidNuevo, bool esRoot,
                                  std::set<int> &visitados, ResultadoChown &resultado) {
    if (!visitados.insert(inoNum).second) return;

    inodo actual{};
    if (!leerInodo(ctx, inoNum, actual)) {
        ++resultado.omitidos;
        return;
    }

    // La capacidad de recorrer el directorio se evalúa antes de cambiar el dueño,
    // porque el cambio podría alterar qué columna de permisos se aplica al usuario.
    const bool puedeRecorrer = actual.i_type == 0 && puedeLeer(actual, uidActual, gidActual);
    const bool autorizado = esRoot || actual.i_uid == uidActual;

    if (autorizado) {
        actual.i_uid = uidNuevo;
        actual.i_ctime = std::time(nullptr);
        if (escribirInodo(ctx, inoNum, actual)) ++resultado.modificados;
        else ++resultado.omitidos;
    } else {
        ++resultado.omitidos;
    }

    if (actual.i_type != 0) return;
    if (!puedeRecorrer && !esRoot) {
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
        cambiarPropietarioRecursivo(ctx, entrada.second, uidActual, gidActual,
                                    uidNuevo, esRoot, visitados, resultado);
    }
}
}

void chown_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path, nombreUsuario;
    bool recursivo = false;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            if (lower(parametros[i]) == "r") {
                recursivo = true;
                continue;
            }
            std::cout << "[ERROR] CHOWN reconoce la bandera -r, -path y -usuario.\n";
            return;
        }
        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "usuario") nombreUsuario = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en CHOWN: -" << clave << "\n";
            return;
        }
    }
    if (path.empty() || nombreUsuario.empty()) {
        std::cout << "[ERROR] CHOWN requiere -path y -usuario.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }

    const auto usuarios = leerUsers(ctx);
    int uidNuevo = -1;
    for (const auto &registro : usuarios) {
        if (registro.id > 0 && registro.type == 'U' && registro.user == nombreUsuario) {
            uidNuevo = registro.id;
            break;
        }
    }
    if (uidNuevo < 0) {
        std::cout << "[ERROR] El usuario destino '" << nombreUsuario << "' no existe.\n";
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
        std::cout << "[ERROR] No se pudo leer el elemento.\n";
        return;
    }
    const bool esRoot = estado.sesion.uid == 1;
    if (!esRoot && objetivo.i_uid != estado.sesion.uid) {
        std::cout << "[ERROR] Solo root o el propietario pueden cambiar el propietario del elemento.\n";
        return;
    }
    if (recursivo && !esRoot && !puedeLeer(objetivo, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] CHOWN -r requiere permiso de lectura en la carpeta inicial.\n";
        return;
    }

    ResultadoChown resultado;
    if (recursivo) {
        std::set<int> visitados;
        cambiarPropietarioRecursivo(ctx, inoNum, estado.sesion.uid, estado.sesion.gid,
                                    uidNuevo, esRoot, visitados, resultado);
    } else {
        objetivo.i_uid = uidNuevo;
        objetivo.i_ctime = std::time(nullptr);
        if (escribirInodo(ctx, inoNum, objetivo)) ++resultado.modificados;
    }

    if (resultado.modificados == 0) {
        std::cout << "[ERROR] No se modifico ningun propietario; compruebe los permisos.\n";
        return;
    }

    if (esExt3(ctx) && !registrarJournal(ctx, "chown", ruta,
                                         "usuario=" + nombreUsuario + (recursivo ? ",r=true" : ""))) {
        std::cout << "[AVISO] Se cambio el propietario, pero no se pudo registrar en journaling.\n";
    }
    std::cout << "[EXITOSO] Propietario actualizado a '" << nombreUsuario << "' en "
              << resultado.modificados << " elemento(s).\n";
    if (resultado.omitidos > 0) {
        std::cout << "[AVISO] Se omitieron " << resultado.omitidos
                  << " elemento(s) por permisos o errores de lectura/escritura.\n";
    }
}
