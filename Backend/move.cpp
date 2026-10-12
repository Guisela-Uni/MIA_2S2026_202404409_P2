#include "move.h"

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

bool esRutaInterna(const std::string &posiblePadre, const std::string &ruta) {
    if (posiblePadre == "/") return ruta == "/" || (!ruta.empty() && ruta.front() == '/');
    return ruta == posiblePadre ||
           (ruta.size() > posiblePadre.size() && ruta.compare(0, posiblePadre.size(), posiblePadre) == 0 &&
            ruta[posiblePadre.size()] == '/');
}
}

void move_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path, destino;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            std::cout << "[ERROR] MOVE requiere -path y -destino.\n";
            return;
        }
        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "destino") destino = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en MOVE: -" << clave << "\n";
            return;
        }
    }
    if (path.empty() || destino.empty()) {
        std::cout << "[ERROR] MOVE requiere -path y -destino.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    const std::string rutaOrigen = normalizarRuta(path);
    const std::string rutaDestino = normalizarRuta(destino);
    if (rutaOrigen == "/" || esRutaInterna(rutaOrigen, rutaDestino)) {
        std::cout << "[ERROR] No se puede mover la raiz ni mover una carpeta dentro de si misma.\n";
        return;
    }

    int origenIno = -1, padreOrigen = -1, destinoIno = -1;
    std::string nombre;
    if (!resolverRuta(ctx, rutaOrigen, origenIno, &padreOrigen, &nombre) || padreOrigen < 0) {
        std::cout << "[ERROR] No existe la ruta origen o no se puede mover la raiz.\n";
        return;
    }
    if (origenIno <= 1) {
        std::cout << "[ERROR] No se permite mover la raiz ni users.txt.\n";
        return;
    }
    if (!resolverRuta(ctx, rutaDestino, destinoIno)) {
        std::cout << "[ERROR] No existe la carpeta destino.\n";
        return;
    }

    inodo origen{}, carpetaDestino{};
    if (!leerInodo(ctx, origenIno, origen) ||
        !puedeEscribir(origen, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de escritura sobre el elemento origen.\n";
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
        std::cout << "[ERROR] No se puede mover una carpeta dentro de si misma o de uno de sus descendientes.\n";
        return;
    }

    if (padreOrigen == destinoIno) {
        std::cout << "[INFO] El elemento ya se encuentra en la carpeta destino.\n";
        return;
    }

    int existente = -1;
    if (buscarEntrada(ctx, destinoIno, nombre, existente)) {
        std::cout << "[ERROR] Ya existe un elemento llamado " << nombre << " en el destino.\n";
        return;
    }

    if (!agregarEntrada(ctx, destinoIno, nombre, origenIno)) {
        std::cout << "[ERROR] No se pudo agregar la referencia en el destino (puede estar lleno).\n";
        return;
    }

    if (origen.i_type == 0 && !actualizarPadreDirectorio(ctx, origenIno, destinoIno)) {
        eliminarEntrada(ctx, destinoIno, nombre);
        std::cout << "[ERROR] No se pudo actualizar la referencia '..' del directorio.\n";
        return;
    }
    if (!eliminarEntrada(ctx, padreOrigen, nombre)) {
        if (origen.i_type == 0) actualizarPadreDirectorio(ctx, origenIno, padreOrigen);
        eliminarEntrada(ctx, destinoIno, nombre);
        std::cout << "[ERROR] No se pudo quitar la referencia del directorio origen.\n";
        return;
    }

    if (esExt3(ctx) && !registrarJournal(ctx, "move", rutaOrigen, "destino=" + rutaDestino)) {
        std::cout << "[AVISO] Se movio el elemento, pero no se pudo registrar en journaling.\n";
    }
    std::cout << "[EXITOSO] Elemento movido: " << rutaOrigen << " -> " << rutaDestino << "/" << nombre << "\n";
}
