#include "find.h"

#include "ext2_utils.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <set>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string patronRegex(const std::string &patron) {
    std::string regex = "^";
    for (char c : patron) {
        if (c == '?') {
            regex += '.';
        } else if (c == '*') {
            regex += ".+"; // El enunciado define * como uno o mas caracteres.
        } else {
            if (std::string("\\.^$|()[]{}+").find(c) != std::string::npos) regex += '\\';
            regex += c;
        }
    }
    regex += '$';
    return regex;
}

std::string unirRuta(const std::string &base, const std::string &nombre) {
    return base == "/" ? "/" + nombre : base + "/" + nombre;
}

void buscarRecursivo(const Ext2Context &ctx, int dirIno, const std::string &ruta,
                     const std::regex &patron, int uid, int gid,
                     std::set<int> &visitados, std::vector<std::string> &resultados) {
    if (!visitados.insert(dirIno).second) return;
    inodo directorio{};
    if (!leerInodo(ctx, dirIno, directorio) || directorio.i_type != 0 ||
        !puedeLeer(directorio, uid, gid)) return;

    std::vector<std::pair<std::string, int>> entradas;
    if (!listarEntradasDirectorio(ctx, dirIno, entradas)) return;
    for (const auto &entrada : entradas) {
        if (entrada.first == "." || entrada.first == "..") continue;
        inodo hijo{};
        if (!leerInodo(ctx, entrada.second, hijo) ||
            !puedeLeer(hijo, uid, gid)) continue;

        const std::string rutaHija = unirRuta(ruta, entrada.first);
        if (std::regex_match(entrada.first, patron)) resultados.push_back(rutaHija);
        if (hijo.i_type == 0) {
            buscarRecursivo(ctx, entrada.second, rutaHija, patron, uid, gid, visitados, resultados);
        }
    }
}
}

void find_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string path, nombre;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos) {
            std::cout << "[ERROR] FIND requiere -path y -name.\n";
            return;
        }
        const std::string clave = lower(parametros[i].substr(0, pos));
        const std::string valor = parametros[i].substr(pos + 1);
        if (clave == "path") path = valor;
        else if (clave == "name") nombre = valor;
        else {
            std::cout << "[ERROR] Parametro no reconocido en FIND: -" << clave << "\n";
            return;
        }
    }
    if (path.empty() || nombre.empty()) {
        std::cout << "[ERROR] FIND requiere -path y -name.\n";
        return;
    }

    Ext2Context ctx;
    if (!sesionActiva(estado, ctx)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    const std::string ruta = normalizarRuta(path);
    int inicio = -1;
    if (!resolverRuta(ctx, ruta, inicio)) {
        std::cout << "[ERROR] No existe la carpeta inicial: " << ruta << "\n";
        return;
    }
    inodo directorio{};
    if (!leerInodo(ctx, inicio, directorio) || directorio.i_type != 0) {
        std::cout << "[ERROR] -path debe ser una carpeta.\n";
        return;
    }
    if (!puedeLeer(directorio, estado.sesion.uid, estado.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de lectura en la carpeta inicial.\n";
        return;
    }

    try {
        const std::regex patron(patronRegex(nombre), std::regex::ECMAScript);
        std::set<int> visitados;
        std::vector<std::string> resultados;
        buscarRecursivo(ctx, inicio, ruta, patron, estado.sesion.uid,
                        estado.sesion.gid, visitados, resultados);
        if (resultados.empty()) {
            std::cout << "[INFO] No se encontraron elementos que coincidan con '" << nombre << "'.\n";
            return;
        }
        std::cout << "[EXITOSO] Resultados de FIND (" << resultados.size() << "): \n";
        for (const std::string &resultado : resultados) std::cout << "  " << resultado << "\n";
    } catch (const std::regex_error &) {
        std::cout << "[ERROR] El patron de busqueda no es valido.\n";
    }
}
