#include "journaling.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>

namespace {

constexpr std::streamoff desplazamientoJournal = sizeof(sbloque);

void copiarTexto(char *destino, std::size_t capacidad, const std::string &origen) {
    if (capacidad == 0) {
        return;
    }

    std::memset(destino, 0, capacidad);
    const std::size_t longitud = std::min(capacidad - 1, origen.size());
    std::memcpy(destino, origen.data(), longitud);
}

bool leerRegistro(const Ext2Context &ctx, int indice, journaling &registro) {
    std::ifstream archivo(ctx.path, std::ios::binary);
    if (!archivo) {
        return false;
    }

    const std::streamoff offset = ctx.start + desplazamientoJournal
        + static_cast<std::streamoff>(indice) * sizeof(journaling);

    archivo.seekg(offset);
    archivo.read(reinterpret_cast<char *>(&registro), sizeof(registro));
    return static_cast<bool>(archivo);
}

} // namespace

bool esExt3(const Ext2Context &ctx) {
    return ctx.sb.s_filesystem_type == 3;
}

bool registrarJournal(
    const Ext2Context &ctx,
    const std::string &operacion,
    const std::string &path,
    const std::string &contenido
) {
    if (!esExt3(ctx)) {
        return false;
    }

    std::fstream archivo(ctx.path, std::ios::in | std::ios::out | std::ios::binary);
    if (!archivo) {
        return false;
    }

    for (int indice = 0; indice < JOURNALING_SIZE; ++indice) {
        journaling registro{};
        const std::streamoff offset = ctx.start + desplazamientoJournal
            + static_cast<std::streamoff>(indice) * sizeof(journaling);

        archivo.seekg(offset);
        archivo.read(reinterpret_cast<char *>(&registro), sizeof(registro));
        if (!archivo) {
            return false;
        }

        if (registro.j_count != 0) {
            continue;
        }

        registro.j_count = indice + 1;
        copiarTexto(registro.j_content.i_operation,
                    sizeof(registro.j_content.i_operation), operacion);
        copiarTexto(registro.j_content.i_path,
                    sizeof(registro.j_content.i_path), path);
        copiarTexto(registro.j_content.i_content,
                    sizeof(registro.j_content.i_content), contenido);
        registro.j_content.i_date = static_cast<float>(std::time(nullptr));

        archivo.clear();
        archivo.seekp(offset);
        archivo.write(reinterpret_cast<const char *>(&registro), sizeof(registro));
        archivo.flush();
        return static_cast<bool>(archivo);
    }

    return false; // El journal tiene una capacidad fija de 50 entradas.
}

std::vector<journaling> leerJournal(const Ext2Context &ctx) {
    std::vector<journaling> entradas;
    if (!esExt3(ctx)) {
        return entradas;
    }

    for (int indice = 0; indice < JOURNALING_SIZE; ++indice) {
        journaling registro{};
        if (!leerRegistro(ctx, indice, registro)) {
            entradas.clear();
            return entradas;
        }

        if (registro.j_count != 0) {
            entradas.push_back(registro);
        }
    }

    return entradas;
}
