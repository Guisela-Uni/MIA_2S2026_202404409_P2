#include "copy.h"

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

std::string nombreBase(const std::string &path) {
    const std::size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool esRutaInterna(const std::string &posiblePadre, const std::string &ruta) {
    if (posiblePadre == "/") return ruta == "/" || (!ruta.empty() && ruta.front() == '/');
    return ruta == posiblePadre ||
           (ruta.size() > posiblePadre.size() && ruta.compare(0, posiblePadre.size(), posiblePadre) == 0 &&
            ruta[posiblePadre.size()] == '/');
}

bool copiarNodo(Ext2Context &ctx, int origenIno, int destinoPadre,
                const std::string &nombreDestino, int uid, int gid,
                std::set<int> &cadenaRecursion, bool &seOmitieronElementos) {
    if (nombreDestino.empty() || nombreDestino.size() > 11) return false;
    if (cadenaRecursion.count(origenIno) != 0) return false;

    inodo origen{};
    if (!leerInodo(ctx, origenIno, origen) || !puedeLeer(origen, uid, gid)) {
        seOmitieronElementos = true;
        return false;
    }

    int repetido = -1;
    if (buscarEntrada(ctx, destinoPadre, nombreDestino, repetido)) return false;

    int nuevoIno = -1;
    if (origen.i_type == 1) {
        const std::string contenido = leerArchivo(ctx, origen);
        if (!crearArchivo(ctx, destinoPadre, nombreDestino, contenido, origen.i_uid, origen.i_gid, nuevoIno)) {
            return false;
        }
        inodo copia{};
        if (leerInodo(ctx, nuevoIno, copia)) {
            copia.i_perm = origen.i_perm;
            copia.i_uid = origen.i_uid;
            copia.i_gid = origen.i_gid;
            escribirInodo(ctx, nuevoIno, copia);
        }
        return true;
    }

    std::vector<std::pair<std::string, int>> entradas;
    if (!listarEntradasDirectorio(ctx, origenIno, entradas)) return false;
    if (!crearDirectorio(ctx, destinoPadre, nombreDestino, origen.i_uid, origen.i_gid, nuevoIno)) {
        return false;
    }
    inodo copiaDir{};
    if (leerInodo(ctx, nuevoIno, copiaDir)) {
        copiaDir.i_perm = origen.i_perm;
        copiaDir.i_uid = origen.i_uid;
        copiaDir.i_gid = origen.i_gid;
        escribirInodo(ctx, nuevoIno, copiaDir);
    }

    cadenaRecursion.insert(origenIno);
    for (const auto &entrada : entradas) {
        if (entrada.first == "." || entrada.first == "..") continue;
        inodo hijo{};
        if (!leerInodo(ctx, entrada.second, hijo) || !puedeLeer(hijo, uid, gid)) {
            seOmitieronElementos = true;
            std::cout << "[AVISO] COPY omitio " << entrada.first << " por falta de permisos de lectura.\n";
            continue;
        }
        if (!copiarNodo(ctx, entrada.second, nuevoIno, entrada.first, uid, gid,
                        cadenaRecursion, seOmitieronElementos)) {
            std::cout << "[AVISO] COPY no pudo copiar " << entrada.first << ".\n";
        }
    }
    cadenaRecursion.erase(origenIno);
    return true;
}
}

void copy_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path, destino;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            std::cout << "[ERROR] COPY requiere -path y -destino.\n";
            return;
        }
        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "destino") destino = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en COPY: -" << clave << "\n";
            return;
        }
    }
    if (path.empty() || destino.empty()) {
        std::cout << "[ERROR] COPY requiere -path y -destino.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    const std::string rutaOrigen = normalizarRuta(path);
    const std::string rutaDestino = normalizarRuta(destino);
    if (rutaOrigen == "/") {
        std::cout << "[ERROR] COPY requiere un archivo o directorio diferente de la raiz.\n";
        return;
    }
    if (esRutaInterna(rutaOrigen, rutaDestino)) {
        std::cout << "[ERROR] No se puede copiar una carpeta dentro de si misma o de uno de sus descendientes.\n";
        return;
    }

    int origenIno = -1, destinoIno = -1;
    if (!resolverRuta(ctx, rutaOrigen, origenIno)) {
        std::cout << "[ERROR] No existe la ruta origen: " << rutaOrigen << "\n";
        return;
    }
    if (!resolverRuta(ctx, rutaDestino, destinoIno)) {
        std::cout << "[ERROR] No existe la carpeta destino: " << rutaDestino << "\n";
        return;
    }
    inodo origen{}, carpetaDestino{};
    if (!leerInodo(ctx, origenIno, origen) || !puedeLeer(origen, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de lectura sobre el elemento origen.\n";
        return;
    }
    if (!leerInodo(ctx, destinoIno, carpetaDestino) || carpetaDestino.i_type != 0) {
        std::cout << "[ERROR] DESTINO debe ser una carpeta existente.\n";
        return;
    }
    if (!puedeEscribir(carpetaDestino, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de escritura sobre la carpeta destino.\n";
        return;
    }

    if (origen.i_type == 0 && esDescendienteDirectorio(ctx, origenIno, destinoIno)) {
        std::cout << "[ERROR] No se puede copiar una carpeta dentro de si misma o de uno de sus descendientes.\n";
        return;
    }

    const std::string nombre = nombreBase(rutaOrigen);
    int existente = -1;
    if (buscarEntrada(ctx, destinoIno, nombre, existente)) {
        std::cout << "[ERROR] Ya existe " << nombre << " en el destino.\n";
        return;
    }

    std::set<int> cadenaRecursion;
    bool omitidos = false;
    if (!copiarNodo(ctx, origenIno, destinoIno, nombre, estado.sesion.uid,
                    estado.sesion.gid, cadenaRecursion, omitidos)) {
        std::cout << "[ERROR] No se pudo copiar el elemento. Compruebe permisos y espacio libre.\n";
        return;
    }

    if (esExt3(ctx) && !registrarJournal(ctx, "copy", rutaOrigen, "destino=" + rutaDestino)) {
        std::cout << "[AVISO] Se copio el elemento, pero no se pudo registrar en journaling.\n";
    }
    const std::string rutaCopia = rutaDestino == "/"
        ? "/" + nombre
        : rutaDestino + "/" + nombre;
    std::cout << "[EXITOSO] Copia creada en " << rutaCopia << ".\n";
    if (omitidos) std::cout << "[AVISO] La copia no incluye los elementos sin permiso de lectura.\n";
}
