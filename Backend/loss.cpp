#include "loss.h"

#include "ext2_utils.h"
#include "journaling.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool limpiarRango(const std::string &ruta, std::streamoff inicio, std::streamoff cantidad) {
    std::fstream archivo(ruta, std::ios::in | std::ios::out | std::ios::binary);
    if (!archivo || inicio < 0 || cantidad < 0) return false;
    archivo.seekp(inicio);
    const std::vector<char> ceros(64 * 1024, '\0');
    while (cantidad > 0) {
        const std::streamsize bloque = static_cast<std::streamsize>(
            std::min<std::streamoff>(cantidad, static_cast<std::streamoff>(ceros.size())));
        archivo.write(ceros.data(), bloque);
        if (!archivo) return false;
        cantidad -= bloque;
    }
    archivo.flush();
    return static_cast<bool>(archivo);
}

bool leerBitmap(const std::string &ruta, std::streamoff inicio, int cantidad,
                std::vector<char> &bitmap) {
    bitmap.assign(static_cast<std::size_t>(cantidad), 0);
    std::ifstream archivo(ruta, std::ios::binary);
    if (!archivo) return false;
    archivo.seekg(inicio);
    archivo.read(bitmap.data(), static_cast<std::streamsize>(bitmap.size()));
    return archivo.gcount() == static_cast<std::streamsize>(bitmap.size());
}

void mostrarBitmap(const std::string &nombre, const std::vector<char> &bitmap) {
    std::size_t ocupados = 0;
    for (char valor : bitmap) if (valor != 0) ++ocupados;

    std::cout << "  " << nombre << ": " << bitmap.size() << " entradas; ocupadas="
              << ocupados << ", libres=" << (bitmap.size() - ocupados) << "\n";
    const std::size_t vista = 64;
    const std::size_t prefijo = std::min(vista, bitmap.size());
    std::cout << "    Inicio: ";
    for (std::size_t i = 0; i < prefijo; ++i) std::cout << (bitmap[i] == 0 ? '0' : '1');
    if (bitmap.size() > vista * 2) {
        std::cout << " ... ";
        for (std::size_t i = bitmap.size() - vista; i < bitmap.size(); ++i) {
            std::cout << (bitmap[i] == 0 ? '0' : '1');
        }
    } else if (bitmap.size() > vista) {
        for (std::size_t i = vista; i < bitmap.size(); ++i) std::cout << (bitmap[i] == 0 ? '0' : '1');
    }
    std::cout << "\n";
}
}

void loss_cmd(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    std::string id;
    for (std::size_t i = 1; i < parametros.size(); ++i) {
        const auto pos = parametros[i].find('=');
        if (pos == std::string::npos || lower(parametros[i].substr(0, pos)) != "id") {
            std::cout << "[ERROR] LOSS solo acepta -id=VALOR.\n";
            return;
        }
        id = parametros[i].substr(pos + 1);
    }
    if (id.empty()) {
        std::cout << "[ERROR] LOSS requiere -id.\n";
        return;
    }

    Ext2Context ctx;
    if (!obtenerContexto(estado, id, ctx)) {
        std::cout << "[ERROR] No existe una particion montada y formateada con ID " << id << ".\n";
        return;
    }
    if (!esExt3(ctx)) {
        std::cout << "[ERROR] LOSS esta definido para particiones EXT3.\n";
        return;
    }

    const std::streamoff bitmapInodosInicio = ctx.sb.s_bm_inode_start;
    const std::streamoff bitmapBloquesInicio = ctx.sb.s_bm_block_start;
    const std::streamoff areaInodosInicio = ctx.sb.s_inode_start;
    const std::streamoff areaBloquesInicio = ctx.sb.s_block_start;
    const std::streamoff bytesInodos = static_cast<std::streamoff>(ctx.sb.s_inodes_count) * sizeof(inodo);
    const std::streamoff bytesBloques = static_cast<std::streamoff>(ctx.sb.s_blocks_count) * sizeof(bcarpetas);
    const std::streamoff finParticion = static_cast<std::streamoff>(ctx.start) + ctx.size;

    if (ctx.sb.s_inodes_count <= 0 || ctx.sb.s_blocks_count <= 0 ||
        bitmapInodosInicio < ctx.start || bitmapBloquesInicio < bitmapInodosInicio ||
        areaInodosInicio < bitmapBloquesInicio || areaBloquesInicio < areaInodosInicio ||
        areaBloquesInicio + bytesBloques > finParticion) {
        std::cout << "[ERROR] Las posiciones del superbloque no son validas; LOSS fue cancelado.\n";
        return;
    }

    std::vector<char> bitmapInodos, bitmapBloques;
    if (!leerBitmap(ctx.path, bitmapInodosInicio, ctx.sb.s_inodes_count, bitmapInodos) ||
        !leerBitmap(ctx.path, bitmapBloquesInicio, ctx.sb.s_blocks_count, bitmapBloques)) {
        std::cout << "[ERROR] No se pudieron leer los bitmaps antes de LOSS.\n";
        return;
    }

    std::cout << "[REPORTE] Bitmaps ANTES de simular la perdida en " << id << ":\n";
    mostrarBitmap("Bitmap de inodos", bitmapInodos);
    mostrarBitmap("Bitmap de bloques", bitmapBloques);

    // El area de journaling esta antes de los bitmaps y se conserva intencionalmente.
    const bool ok =
        limpiarRango(ctx.path, bitmapInodosInicio, ctx.sb.s_inodes_count) &&
        limpiarRango(ctx.path, bitmapBloquesInicio, ctx.sb.s_blocks_count) &&
        limpiarRango(ctx.path, areaInodosInicio, bytesInodos) &&
        limpiarRango(ctx.path, areaBloquesInicio, bytesBloques);
    if (!ok) {
        std::cout << "[ERROR] Fallo de escritura mientras se limpiaban las areas de datos.\n";
        return;
    }

    // Se mantiene el superbloque y el journal para identificar el filesystem y conservar la bitacora.
    ctx.sb.s_free_inodes_count = ctx.sb.s_inodes_count;
    ctx.sb.s_free_blocks_count = ctx.sb.s_blocks_count;
    ctx.sb.s_first_ino = 0;
    ctx.sb.s_first_blo = 0;
    if (!guardarSuper(ctx)) {
        std::cout << "[ERROR] Las areas se limpiaron, pero no se pudo actualizar el superbloque.\n";
        return;
    }

    if (estado.sesion.activa && estado.sesion.id == id) estado.sesion = {};

    // Se conserva el area de journal durante LOSS y se registra la simulacion
    // al final, despues de limpiar las estructuras de datos del filesystem.
    if (esExt3(ctx) && !registrarJournal(ctx, "loss", "/",
                                         "bitmaps, inodos y bloques limpiados")) {
        std::cout << "[AVISO] LOSS termino, pero no se pudo registrar la operacion en journaling.\n";
    }

    leerBitmap(ctx.path, bitmapInodosInicio, ctx.sb.s_inodes_count, bitmapInodos);
    leerBitmap(ctx.path, bitmapBloquesInicio, ctx.sb.s_blocks_count, bitmapBloques);
    std::cout << "[REPORTE] Bitmaps DESPUES de simular la perdida en " << id << ":\n";
    mostrarBitmap("Bitmap de inodos", bitmapInodos);
    mostrarBitmap("Bitmap de bloques", bitmapBloques);
    std::cout << "[EXITOSO] LOSS finalizado. El superbloque y el journaling EXT3 se conservaron; "
                 "se limpiaron bitmaps, inodos y bloques.\n";
}
