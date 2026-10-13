#include "fdisk.h"

#include "disk_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
struct LogicaEncontrada {
    int posicionEBR = -1;
    int posicionAnterior = -1;
    int indiceExtendida = -1;
    EBR ebr{};
};

std::string lower(std::string valor) {
    std::transform(valor.begin(), valor.end(), valor.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return valor;
}

bool separarParametro(const std::string &token, std::string &clave, std::string &valor) {
    const std::size_t pos = token.find('=');
    if (pos == std::string::npos) return false;
    clave = lower(token.substr(0, pos));
    valor = token.substr(pos + 1);
    return true;
}

char convertirFit(const std::string &valor) {
    const std::string fit = lower(valor);
    if (fit == "bf") return 'b';
    if (fit == "ff") return 'f';
    if (fit == "wf") return 'w';
    return '?';
}

char convertirTipo(const std::string &valor) {
    const std::string tipo = lower(valor);
    if (tipo == "p") return 'p';
    if (tipo == "e") return 'e';
    if (tipo == "l") return 'l';
    return '?';
}

bool convertirBytes(long long valor, const std::string &unidad, long long &bytes) {
    const std::string u = lower(unidad);
    long long factor = 1;
    if (u == "b") factor = 1;
    else if (u == "k") factor = 1024;
    else if (u == "m") factor = 1024LL * 1024LL;
    else return false;

    if (valor > std::numeric_limits<long long>::max() / factor ||
        valor < std::numeric_limits<long long>::min() / factor) return false;
    bytes = valor * factor;
    return bytes >= std::numeric_limits<int>::min() && bytes <= std::numeric_limits<int>::max();
}

bool limpiarRango(const std::string &ruta, int inicio, int cantidad) {
    if (inicio < 0 || cantidad < 0) return false;
    std::fstream archivo(ruta, std::ios::in | std::ios::out | std::ios::binary);
    if (!archivo) return false;
    archivo.seekp(inicio);
    const std::vector<char> ceros(64 * 1024, '\0');
    int restantes = cantidad;
    while (restantes > 0) {
        const int bloque = std::min<int>(restantes, static_cast<int>(ceros.size()));
        archivo.write(ceros.data(), bloque);
        if (!archivo) return false;
        restantes -= bloque;
    }
    archivo.flush();
    return static_cast<bool>(archivo);
}

bool estaMontada(const EstadoGlobal &estado, const std::string &ruta, const std::string &nombre) {
    for (const auto &discoMontado : estado.discosMontados) {
        if (discoMontado.ruta != ruta) continue;
        for (const auto &particionMontada : discoMontado.particiones) {
            if (particionMontada.nombre == nombre) return true;
        }
    }
    return false;
}

bool hayLogicasMontadasEnExtendida(const EstadoGlobal &estado, const std::string &ruta,
                                   const particion &extendida) {
    const long long inicio = extendida.part_start;
    const long long fin = inicio + extendida.part_s;
    for (const auto &discoMontado : estado.discosMontados) {
        if (discoMontado.ruta != ruta) continue;
        for (const auto &particionMontada : discoMontado.particiones) {
            if (particionMontada.posEBR >= inicio && particionMontada.posEBR < fin) {
                return true;
            }
        }
    }
    return false;
}

bool buscarLogicaCompleta(const std::string &ruta, const SimDisk &disco,
                          const std::string &nombre, LogicaEncontrada &resultado) {
    int indiceExtendida = -1;
    if (!buscarExtendida(disco, indiceExtendida)) return false;

    const particion &extendida = disco.particiones[indiceExtendida];
    const int inicio = extendida.part_start;
    const int fin = extendida.part_start + extendida.part_s;
    int posicion = inicio;
    int anterior = -1;

    for (int visitas = 0; visitas < 100000 && posicion >= inicio &&
         posicion + static_cast<int>(sizeof(EBR)) <= fin; ++visitas) {
        EBR actual{};
        if (!leerEBR(ruta, posicion, actual)) return false;
        if (actual.part_s > 0 && std::string(actual.part_name) == nombre) {
            resultado.posicionEBR = posicion;
            resultado.posicionAnterior = anterior;
            resultado.indiceExtendida = indiceExtendida;
            resultado.ebr = actual;
            return true;
        }
        if (actual.part_next == -1 || actual.part_next <= posicion ||
            actual.part_next + static_cast<int>(sizeof(EBR)) > fin) break;
        anterior = posicion;
        posicion = actual.part_next;
    }
    return false;
}

bool nombreYaExiste(const std::string &ruta, const SimDisk &disco, const std::string &nombre) {
    for (const auto &particion : disco.particiones) {
        if ((particion.part_type == 'p' || particion.part_type == 'e') &&
            std::string(particion.part_name) == nombre) return true;
    }
    LogicaEncontrada logica;
    return buscarLogicaCompleta(ruta, disco, nombre, logica);
}

bool espacioParaParticion(const SimDisk &disco, int bytes, char fit, int &inicioElegido) {
    std::vector<std::pair<int, int>> ocupados;
    for (const auto &particion : disco.particiones) {
        if (particion.part_type != 'p' && particion.part_type != 'e') continue;
        if (particion.part_s <= 0 || particion.part_start < 0) continue;
        ocupados.emplace_back(particion.part_start, particion.part_start + particion.part_s);
    }
    std::sort(ocupados.begin(), ocupados.end());

    std::vector<std::pair<int, int>> libres;
    int cursor = EndMBR;
    for (const auto &rango : ocupados) {
        if (rango.first > cursor) libres.emplace_back(cursor, rango.first - cursor);
        cursor = std::max(cursor, rango.second);
    }
    if (cursor < disco.tamano) libres.emplace_back(cursor, disco.tamano - cursor);

    int seleccionado = -1;
    for (int i = 0; i < static_cast<int>(libres.size()); ++i) {
        if (libres[i].second < bytes) continue;
        if (seleccionado < 0) {
            seleccionado = i;
            if (fit == 'f') break;
            continue;
        }
        if (fit == 'b' && libres[i].second < libres[seleccionado].second) seleccionado = i;
        if (fit == 'w' && libres[i].second > libres[seleccionado].second) seleccionado = i;
    }
    if (seleccionado < 0) return false;
    inicioElegido = libres[seleccionado].first;
    return true;
}

bool crearLogica(const std::string &ruta, SimDisk &disco, int indiceExtendida,
                 int bytes, char fit, const std::string &nombre) {
    const particion &extendida = disco.particiones[indiceExtendida];
    const int inicio = extendida.part_start;
    const int fin = extendida.part_start + extendida.part_s;
    int posicion = inicio;

    EBR primero{};
    if (!leerEBR(ruta, inicio, primero)) return false;
    if (primero.part_s == 0 && primero.part_next == -1) {
        if (inicio + static_cast<int>(sizeof(EBR)) + bytes > fin) return false;
        EBR nueva{};
        nueva.part_status = '0';
        nueva.part_fit = fit;
        nueva.part_start = inicio + static_cast<int>(sizeof(EBR));
        nueva.part_s = bytes;
        nueva.part_next = -1;
        std::strncpy(nueva.part_name, nombre.c_str(), sizeof(nueva.part_name) - 1);
        return escribirEBR(ruta, inicio, nueva);
    }

    for (int visitas = 0; visitas < 100000; ++visitas) {
        EBR actual{};
        if (posicion < inicio || posicion + static_cast<int>(sizeof(EBR)) > fin ||
            !leerEBR(ruta, posicion, actual)) return false;

        if (actual.part_s > 0) {
            const int finDatos = actual.part_start + actual.part_s;
            if (actual.part_next != -1) {
                if (finDatos + static_cast<int>(sizeof(EBR)) + bytes <= actual.part_next) {
                    EBR nueva{};
                    nueva.part_status = '0';
                    nueva.part_fit = fit;
                    nueva.part_start = finDatos + static_cast<int>(sizeof(EBR));
                    nueva.part_s = bytes;
                    nueva.part_next = actual.part_next;
                    std::strncpy(nueva.part_name, nombre.c_str(), sizeof(nueva.part_name) - 1);
                    actual.part_next = finDatos;
                    return escribirEBR(ruta, finDatos, nueva) && escribirEBR(ruta, posicion, actual);
                }
                if (actual.part_next <= posicion) return false;
                posicion = actual.part_next;
                continue;
            }

            if (finDatos + static_cast<int>(sizeof(EBR)) + bytes > fin) return false;
            EBR nueva{};
            nueva.part_status = '0';
            nueva.part_fit = fit;
            nueva.part_start = finDatos + static_cast<int>(sizeof(EBR));
            nueva.part_s = bytes;
            nueva.part_next = -1;
            std::strncpy(nueva.part_name, nombre.c_str(), sizeof(nueva.part_name) - 1);
            actual.part_next = finDatos;
            return escribirEBR(ruta, finDatos, nueva) && escribirEBR(ruta, posicion, actual);
        }

        return false;
    }
    return false;
}

bool agregarEspacio(const std::string &ruta, SimDisk &disco, const std::string &nombre,
                    int delta, EstadoGlobal &estado) {
    if (delta == 0) {
        std::cout << "[ERROR] -add debe ser distinto de cero.\n";
        return false;
    }
    if (estaMontada(estado, ruta, nombre)) {
        std::cout << "[ERROR] Desmonte la particion antes de cambiar su tamano.\n";
        return false;
    }

    int indice = -1;
    for (int i = 0; i < 4; ++i) {
        if ((disco.particiones[i].part_type == 'p' || disco.particiones[i].part_type == 'e') &&
            std::string(disco.particiones[i].part_name) == nombre) {
            indice = i;
            break;
        }
    }

    if (indice >= 0) {
        particion &objetivo = disco.particiones[indice];
        const long long nuevoTamanoLargo = static_cast<long long>(objetivo.part_s) + delta;
        if (nuevoTamanoLargo <= 0 || nuevoTamanoLargo > std::numeric_limits<int>::max()) {
            std::cout << "[ERROR] El nuevo tamano de la particion debe ser positivo.\n";
            return false;
        }
        const int nuevoTamano = static_cast<int>(nuevoTamanoLargo);

        if (delta > 0) {
            int limite = disco.tamano;
            for (int i = 0; i < 4; ++i) {
                if (i == indice) continue;
                const auto &otra = disco.particiones[i];
                if ((otra.part_type == 'p' || otra.part_type == 'e') &&
                    otra.part_start >= objetivo.part_start + objetivo.part_s) {
                    limite = std::min(limite, otra.part_start);
                }
            }
            if (objetivo.part_start + nuevoTamano > limite) {
                std::cout << "[ERROR] No existe suficiente espacio libre contiguo despues de la particion.\n";
                return false;
            }
        }

        if (objetivo.part_type == 'e') {
            if (hayLogicasMontadasEnExtendida(estado, ruta, objetivo)) {
                std::cout << "[ERROR] Desmonte las particiones logicas antes de cambiar el tamano de la extendida.\n";
                return false;
            }
            const int finNuevo = objetivo.part_start + nuevoTamano;
            int posicion = objetivo.part_start;
            for (int visitas = 0; visitas < 100000 && posicion + static_cast<int>(sizeof(EBR)) <=
                 objetivo.part_start + objetivo.part_s; ++visitas) {
                EBR ebr{};
                if (!leerEBR(ruta, posicion, ebr)) break;
                if (ebr.part_s > 0 && (posicion + static_cast<int>(sizeof(EBR)) > finNuevo ||
                    ebr.part_start + ebr.part_s > finNuevo)) {
                    std::cout << "[ERROR] No se puede reducir la extendida: contiene particiones logicas fuera del nuevo limite.\n";
                    return false;
                }
                if (ebr.part_next == -1 || ebr.part_next <= posicion) break;
                posicion = ebr.part_next;
            }
        }

        objetivo.part_s = nuevoTamano;
        MBR mbr{};
        if (!leerMBR(ruta, mbr)) return false;
        mbr.mbr_partition[indice] = objetivo;
        if (!escribirMBR(ruta, mbr)) return false;
        estado.discos[ruta] = disco;
        std::cout << "[EXITOSO] Tamano actualizado para " << nombre << ": " << nuevoTamano << " bytes.\n";
        return true;
    }

    LogicaEncontrada logica;
    if (!buscarLogicaCompleta(ruta, disco, nombre, logica)) {
        std::cout << "[ERROR] No existe una particion llamada '" << nombre << "'.\n";
        return false;
    }

    const particion &extendida = disco.particiones[logica.indiceExtendida];
    const long long nuevoTamanoLargo = static_cast<long long>(logica.ebr.part_s) + delta;
    if (nuevoTamanoLargo <= 0 || nuevoTamanoLargo > std::numeric_limits<int>::max()) {
        std::cout << "[ERROR] El nuevo tamano de la particion logica debe ser positivo.\n";
        return false;
    }
    const int nuevoTamano = static_cast<int>(nuevoTamanoLargo);
    const int nuevoFin = logica.ebr.part_start + nuevoTamano;
    const int limite = logica.ebr.part_next == -1
        ? extendida.part_start + extendida.part_s
        : logica.ebr.part_next;
    if (nuevoFin > limite) {
        std::cout << "[ERROR] No existe espacio libre contiguo suficiente despues de la particion logica.\n";
        return false;
    }
    logica.ebr.part_s = nuevoTamano;
    if (!escribirEBR(ruta, logica.posicionEBR, logica.ebr)) return false;
    std::cout << "[EXITOSO] Tamano actualizado para la particion logica " << nombre
              << ": " << nuevoTamano << " bytes.\n";
    return true;
}

bool eliminarParticion(const std::string &ruta, SimDisk &disco, const std::string &nombre,
                       const std::string &modo, EstadoGlobal &estado) {
    if (modo != "fast" && modo != "full") {
        std::cout << "[ERROR] -delete solo acepta fast o full.\n";
        return false;
    }
    if (estaMontada(estado, ruta, nombre)) {
        std::cout << "[ERROR] Desmonte la particion antes de eliminarla.\n";
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        particion &objetivo = disco.particiones[i];
        if ((objetivo.part_type != 'p' && objetivo.part_type != 'e') ||
            std::string(objetivo.part_name) != nombre) continue;

        if (objetivo.part_type == 'e' && hayLogicasMontadasEnExtendida(estado, ruta, objetivo)) {
            std::cout << "[ERROR] Desmonte las particiones logicas antes de eliminar la extendida.\n";
            return false;
        }

        if (modo == "full" && !limpiarRango(ruta, objetivo.part_start, objetivo.part_s)) {
            std::cout << "[ERROR] No se pudo limpiar el espacio de la particion.\n";
            return false;
        }
        limpiarParticion(objetivo);
        MBR mbr{};
        if (!leerMBR(ruta, mbr)) return false;
        mbr.mbr_partition[i] = objetivo;
        if (!escribirMBR(ruta, mbr)) return false;
        estado.discos[ruta] = disco;
        estado.particionesFormateadas[ruta + "|" + nombre] = false;
        std::cout << "[EXITOSO] Particion '" << nombre << "' eliminada (" << modo << ").\n";
        return true;
    }

    LogicaEncontrada logica;
    if (!buscarLogicaCompleta(ruta, disco, nombre, logica)) {
        std::cout << "[ERROR] No existe una particion llamada '" << nombre << "'.\n";
        return false;
    }

    const int finDatos = logica.ebr.part_start + logica.ebr.part_s;
    if (logica.posicionAnterior >= 0) {
        EBR anterior{};
        if (!leerEBR(ruta, logica.posicionAnterior, anterior)) return false;
        anterior.part_next = logica.ebr.part_next;
        if (!escribirEBR(ruta, logica.posicionAnterior, anterior)) return false;
        if (modo == "full" && !limpiarRango(ruta, logica.posicionEBR,
                                             finDatos - logica.posicionEBR)) {
            std::cout << "[ERROR] No se pudo limpiar la particion logica.\n";
            return false;
        }
    } else if (logica.ebr.part_next != -1) {
        // El primer EBR ocupa el inicio de la extendida. Se copia el siguiente
        // encabezado al principio para mantener accesible la cadena de logicas.
        EBR siguiente{};
        if (!leerEBR(ruta, logica.ebr.part_next, siguiente)) return false;
        if (modo == "full" && !limpiarRango(ruta, logica.ebr.part_start,
                                             logica.ebr.part_s)) return false;
        if (!escribirEBR(ruta, disco.particiones[logica.indiceExtendida].part_start, siguiente)) {
            return false;
        }
    } else {
        if (modo == "full" && !limpiarRango(ruta, logica.ebr.part_start,
                                             logica.ebr.part_s)) return false;
        EBR vacio{};
        vacio.part_status = '0';
        vacio.part_fit = disco.particiones[logica.indiceExtendida].part_fit;
        vacio.part_start = disco.particiones[logica.indiceExtendida].part_start
            + static_cast<int>(sizeof(EBR));
        vacio.part_s = 0;
        vacio.part_next = -1;
        if (!escribirEBR(ruta, disco.particiones[logica.indiceExtendida].part_start, vacio)) return false;
    }

    estado.particionesFormateadas[ruta + "|" + nombre] = false;
    std::cout << "[EXITOSO] Particion logica '" << nombre << "' eliminada (" << modo << ").\n";
    return true;
}

} // namespace

void fdisk(std::vector<std::string> &parametros, EstadoGlobal &estado) {
    long long size = 0;
    long long add = 0;
    bool tieneSize = false;
    bool tieneAdd = false;
    bool tieneDelete = false;
    std::string unidad = "k";
    std::string ruta;
    std::string tipo = "p";
    std::string fit = "wf";
    std::string nombre;
    std::string modoDelete;
    std::string confirmacion;

    for (std::size_t i = 1; i < parametros.size(); ++i) {
        std::string clave, valor;
        if (!separarParametro(parametros[i], clave, valor)) {
            std::cout << "[ERROR] FDISK requiere parametros clave=valor.\n";
            return;
        }
        if (clave == "size") {
            try { size = std::stoll(valor); } catch (...) {
                std::cout << "[ERROR] size invalido.\n"; return;
            }
            tieneSize = true;
        } else if (clave == "add") {
            try { add = std::stoll(valor); } catch (...) {
                std::cout << "[ERROR] add invalido.\n"; return;
            }
            tieneAdd = true;
        } else if (clave == "delete") {
            modoDelete = lower(valor);
            tieneDelete = true;
        } else if (clave == "confirm") {
            confirmacion = lower(valor);
        } else if (clave == "unit") {
            unidad = valor;
        } else if (clave == "path") {
            ruta = valor;
        } else if (clave == "type") {
            tipo = valor;
        } else if (clave == "fit") {
            fit = valor;
        } else if (clave == "name") {
            nombre = valor;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en FDISK: -" << clave << "\n";
            return;
        }
    }

    if (ruta.empty() || nombre.empty()) {
        std::cout << "[ERROR] FDISK requiere -path y -name.\n";
        return;
    }
    if (nombre.size() > 15) {
        std::cout << "[ERROR] El nombre de particion no puede superar 15 caracteres.\n";
        return;
    }
    if (static_cast<int>(tieneAdd) + static_cast<int>(tieneDelete) > 1) {
        std::cout << "[ERROR] Use solo una opcion entre -add y -delete.\n";
        return;
    }
    if (tieneDelete && modoDelete != "fast" && modoDelete != "full") {
        std::cout << "[ERROR] -delete solo acepta fast o full.\n";
        return;
    }
    if (!confirmacion.empty() && !tieneDelete) {
        std::cout << "[ERROR] -confirm solo se utiliza junto con -delete.\n";
        return;
    }
    if (!confirmacion.empty() && confirmacion != "yes" && confirmacion != "no" &&
        confirmacion != "si" && confirmacion != "s" && confirmacion != "true" &&
        confirmacion != "false") {
        std::cout << "[ERROR] -confirm acepta yes/no.\n";
        return;
    }

    SimDisk disco{};
    if (!cargarDisco(ruta, disco)) {
        std::cout << "[ERROR] No se pudo abrir el disco .mia.\n";
        return;
    }

    if (tieneDelete || tieneAdd) {
        long long bytesLargos = 0;
        if (tieneAdd && !convertirBytes(add, unidad, bytesLargos)) {
            std::cout << "[ERROR] -add o -unit contienen un valor invalido.\n";
            return;
        }
        if (bytesLargos < std::numeric_limits<int>::min() ||
            bytesLargos > std::numeric_limits<int>::max()) {
            std::cout << "[ERROR] El valor calculado excede el rango soportado.\n";
            return;
        }
        if (tieneDelete) {
            if (tieneSize) std::cout << "[AVISO] -size se ignora cuando se utiliza -delete.\n";

            bool existeParticion = false;
            for (const auto &particionActual : disco.particiones) {
                if ((particionActual.part_type == 'p' || particionActual.part_type == 'e') &&
                    std::string(particionActual.part_name) == nombre) {
                    existeParticion = true;
                    break;
                }
            }
            if (!existeParticion) {
                LogicaEncontrada logicaExistente;
                existeParticion = buscarLogicaCompleta(ruta, disco, nombre, logicaExistente);
            }
            if (!existeParticion) {
                std::cout << "[ERROR] No existe una particion llamada '" << nombre << "'.\n";
                return;
            }

            const bool confirmado = confirmacion == "yes" || confirmacion == "si" ||
                                    confirmacion == "s" || confirmacion == "true";
            const bool rechazado = confirmacion == "no" || confirmacion == "false";
            if (rechazado) {
                std::cout << "[INFO] Eliminacion cancelada por el usuario.\n";
                return;
            }
#if defined(API_MODE)
            if (!confirmado) {
                std::cout << "[CONFIRMACION] Para eliminar la particion '" << nombre
                          << "' en modo " << modoDelete
                          << ", confirme la accion y vuelva a enviar el comando con -confirm=yes.\n";
                return;
            }
#else
            if (!confirmado) {
                std::cout << "[CONFIRMACION] ¿Desea eliminar la particion '" << nombre
                          << "' en modo " << modoDelete << "? (s/n): ";
                std::string respuesta;
                std::getline(std::cin, respuesta);
                respuesta = lower(respuesta);
                if (respuesta != "s" && respuesta != "si" && respuesta != "yes" && respuesta != "y") {
                    std::cout << "[INFO] Eliminacion cancelada.\n";
                    return;
                }
            }
#endif
            eliminarParticion(ruta, disco, nombre, modoDelete, estado);
        } else {
            if (tieneSize) std::cout << "[AVISO] -size se ignora cuando se utiliza -add.\n";
            agregarEspacio(ruta, disco, nombre, static_cast<int>(bytesLargos), estado);
        }
        return;
    }

    if (!tieneSize || size <= 0) {
        std::cout << "[ERROR] Para crear una particion, -size debe ser positivo y mayor que cero.\n";
        return;
    }
    long long bytesLargos = 0;
    if (!convertirBytes(size, unidad, bytesLargos) || bytesLargos <= 0 ||
        bytesLargos > std::numeric_limits<int>::max()) {
        std::cout << "[ERROR] -unit debe ser B, K o M y el tamano debe ser valido.\n";
        return;
    }
    const int bytes = static_cast<int>(bytesLargos);
    const char fitChar = convertirFit(fit);
    const char tipoChar = convertirTipo(tipo);
    if (fitChar == '?' || tipoChar == '?') {
        std::cout << "[ERROR] Fit o type invalido en FDISK.\n";
        return;
    }
    if (nombreYaExiste(ruta, disco, nombre)) {
        std::cout << "[ERROR] Ya existe una particion con ese nombre.\n";
        return;
    }

    int indiceExtendida = -1;
    const bool tieneExtendida = buscarExtendida(disco, indiceExtendida);
    if (tipoChar == 'e' && tieneExtendida) {
        std::cout << "[ERROR] Solo puede existir una particion extendida por disco.\n";
        return;
    }
    if (tipoChar == 'l') {
        if (!tieneExtendida) {
            std::cout << "[ERROR] No existe una particion extendida para alojar la logica.\n";
            return;
        }
        if (!crearLogica(ruta, disco, indiceExtendida, bytes, fitChar, nombre)) {
            std::cout << "[ERROR] No hay espacio continuo suficiente en la particion extendida.\n";
            return;
        }
        estado.particionesFormateadas[ruta + "|" + nombre] = false;
        std::cout << "[EXITOSO] Particion logica creada: " << nombre << "\n";
        return;
    }

    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        if (disco.particiones[i].part_type != 'p' && disco.particiones[i].part_type != 'e') {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        std::cout << "[ERROR] No hay slots libres en el MBR.\n";
        return;
    }

    int inicio = -1;
    if (!espacioParaParticion(disco, bytes, fitChar, inicio)) {
        std::cout << "[ERROR] No hay espacio contiguo suficiente en el disco.\n";
        return;
    }

    particion nueva{};
    inicializarParticion(nueva);
    nueva.part_status = '0';
    nueva.part_type = tipoChar;
    nueva.part_fit = fitChar;
    nueva.part_start = inicio;
    nueva.part_s = bytes;
    nueva.part_correlativ = 0;
    std::strncpy(nueva.part_name, nombre.c_str(), sizeof(nueva.part_name) - 1);

    MBR mbr{};
    if (!leerMBR(ruta, mbr)) {
        std::cout << "[ERROR] No se pudo leer el MBR.\n";
        return;
    }
    mbr.mbr_partition[slot] = nueva;
    if (!escribirMBR(ruta, mbr)) {
        std::cout << "[ERROR] No se pudo guardar la particion.\n";
        return;
    }

    disco.particiones[slot] = nueva;
    estado.discos[ruta] = disco;
    estado.particionesFormateadas[ruta + "|" + nombre] = false;
    if (tipoChar == 'e') {
        EBR inicial{};
        inicial.part_status = '0';
        inicial.part_fit = fitChar;
        inicial.part_start = inicio + static_cast<int>(sizeof(EBR));
        inicial.part_s = 0;
        inicial.part_next = -1;
        if (!escribirEBR(ruta, inicio, inicial)) {
            std::cout << "[AVISO] Particion extendida creada, pero no se pudo inicializar su EBR.\n";
            return;
        }
    }
    std::cout << "[EXITOSO] Particion " << (tipoChar == 'e' ? "extendida" : "primaria")
              << " creada: " << nombre << " (" << bytes << " bytes).\n";
}

void particionar(int size, char tipo, const std::string &ruta,
                 const std::string &nombre, char fit, SimDisk &disco) {
    // Se conserva la firma histórica para compatibilidad con módulos previos.
    (void)size;
    (void)tipo;
    (void)ruta;
    (void)nombre;
    (void)fit;
    (void)disco;
}
