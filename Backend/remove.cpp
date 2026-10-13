#include "remove.h"

#include "ext2_utils.h"
#include "journaling.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct ResultadoRemove {
    bool eliminado = false;
    bool completo = false;
};

// Elimina primero los hijos que el usuario puede modificar. Si queda algún hijo
// por permisos o por un error de lectura/escritura, conserva el directorio padre.
ResultadoRemove eliminarNodo(Ext2Context &ctx, int inoNum, int padreIno,
                             const std::string &nombre, int uid, int gid,
                             std::set<int> &visitados) {
    ResultadoRemove resultado;
    if (inoNum <= 1 || !visitados.insert(inoNum).second) {
        resultado.completo = false;
        return resultado;
    }

    inodo actual{};
    if (!leerInodo(ctx, inoNum, actual) || !puedeEscribir(actual, uid, gid)) {
        visitados.erase(inoNum);
        return resultado;
    }

    // La entrada también se modifica en el directorio padre.
    inodo padre{};
    if (!leerInodo(ctx, padreIno, padre) || padre.i_type != 0 ||
        !puedeEscribir(padre, uid, gid)) {
        visitados.erase(inoNum);
        return resultado;
    }

    if (actual.i_type == 0) {
        // Sin permiso de escritura en la carpeta, no se toca ningún hijo.
        std::vector<std::pair<std::string, int>> entradas;
        if (!listarEntradasDirectorio(ctx, inoNum, entradas)) {
            visitados.erase(inoNum);
            return resultado;
        }

        bool todosLosHijosEliminados = true;
        for (const auto &entrada : entradas) {
            if (entrada.first == "." || entrada.first == "..") continue;

            ResultadoRemove hijo = eliminarNodo(
                ctx, entrada.second, inoNum, entrada.first, uid, gid, visitados);
            resultado.eliminado = resultado.eliminado || hijo.eliminado;
            if (!hijo.completo) todosLosHijosEliminados = false;
        }

        // Volver a leer el directorio permite verificar lo que realmente quedó,
        // incluso si una operación hija falló después de cambiar alguna referencia.
        std::vector<std::pair<std::string, int>> restantes;
        if (!listarEntradasDirectorio(ctx, inoNum, restantes)) {
            visitados.erase(inoNum);
            resultado.completo = false;
            return resultado;
        }
        const bool directorioVacio = std::none_of(
            restantes.begin(), restantes.end(), [](const auto &entrada) {
                return entrada.first != "." && entrada.first != "..";
            });
        if (!todosLosHijosEliminados || !directorioVacio) {
            visitados.erase(inoNum);
            resultado.completo = false;
            return resultado;
        }
    }

    // Quitar la referencia antes de liberar inodo y bloques evita dejar una
    // entrada activa apuntando a un inodo que ya fue liberado.
    if (!eliminarEntrada(ctx, padreIno, nombre)) {
        visitados.erase(inoNum);
        resultado.completo = false;
        return resultado;
    }

    if (!liberarArbolInodo(ctx, inoNum)) {
        // La entrada ya fue retirada; se informa la modificación parcial.
        resultado.eliminado = true;
        resultado.completo = false;
        visitados.erase(inoNum);
        return resultado;
    }

    resultado.eliminado = true;
    resultado.completo = true;
    visitados.erase(inoNum);
    return resultado;
}
}

void remove_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos || lower(parametros[i].substr(0, pos)) != "path") {
            std::cout << "[ERROR] REMOVE solo acepta -path=RUTA.\n";
            return;
        }
        path = parametros[i].substr(pos + 1);
    }
    if (path.empty()) {
        std::cout << "[ERROR] REMOVE requiere -path.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }

    const std::string ruta = normalizarRuta(path);
    int inoNum = -1, padre = -1;
    std::string nombre;
    if (ruta == "/") {
        std::cout << "[ERROR] No se puede eliminar la raiz del sistema de archivos.\n";
        return;
    }
    if (!resolverRuta(ctx, ruta, inoNum, &padre, &nombre)) {
        std::cout << "[ERROR] No existe el elemento: " << ruta << "\n";
        return;
    }
    if (padre < 0) {
        std::cout << "[ERROR] No se puede eliminar la raiz del sistema de archivos.\n";
        return;
    }

    std::set<int> visitados;
    const ResultadoRemove resultado = eliminarNodo(
        ctx, inoNum, padre, nombre, estado.sesion.uid, estado.sesion.gid, visitados);

    if (resultado.eliminado && esExt3(ctx) &&
        !registrarJournal(ctx, "remove", ruta,
                         resultado.completo ? "completo" : "parcial")) {
        std::cout << "[AVISO] Hubo cambios, pero no se pudo registrar REMOVE en journaling.\n";
    }

    if (resultado.completo) {
        std::cout << "[EXITOSO] Elemento eliminado: " << ruta << "\n";
    } else if (resultado.eliminado) {
        std::cout << "[AVISO] Se eliminaron los elementos permitidos, pero se conservaron los padres "
                     "que aun contienen elementos no eliminados: " << ruta << "\n";
    } else {
        std::cout << "[ERROR] No se pudo eliminar el elemento. Verifique la ruta y los permisos.\n";
    }
}
