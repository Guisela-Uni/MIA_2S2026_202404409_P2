#include "fdisk.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool param(const std::string& t, std::string& k, std::string& v) {
    auto p = t.find('=');
    if (p == std::string::npos) {
        return false;
    }
    k = lower(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

char fitChar(const std::string& s) {
    auto x = lower(s);
    return x == "bf" ? 'b' : x == "ff" ? 'f' : x == "wf" ? 'w' : '?';
}

char typeChar(const std::string& s) {
    auto x = lower(s);
    return x == "p" ? 'p' : x == "e" ? 'e' : x == "l" ? 'l' : '?';
}

int toBytes(int n, const std::string& u) {
    auto x = lower(u);
    if (x == "b") return n;
    if (x == "k") return n * 1024;
    if (x == "m") return n * 1024 * 1024;
    return -1;
}

int choose(const std::vector<libre>& spaces, int need, char fit) {
    if (fit == 'f') {
        for (int i = 0; i < (int)spaces.size(); ++i) {
            if (spaces[i].size >= need) return i;
        }
    }
    if (fit == 'b') {
        int best = -1;
        for (int i = 0; i < (int)spaces.size(); ++i) {
            if (spaces[i].size >= need && (best < 0 || spaces[i].size < spaces[best].size)) {
                best = i;
            }
        }
        return best;
    }
    int worst = -1;
    for (int i = 0; i < (int)spaces.size(); ++i) {
        if (spaces[i].size >= need && (worst < 0 || spaces[i].size > spaces[worst].size)) {
            worst = i;
        }
    }
    return worst;
}

} // namespace

void fdisk(std::vector<std::string>& p, EstadoGlobal& estado) {
    int size = -1;
    std::string unit = "k", path, type = "p", fit = "wf", name;

    for (size_t i = 1; i < p.size(); ++i) {
        std::string k, v;
        if (!param(p[i], k, v)) {
            std::cout << "[ERROR] FDISK requiere clave=valor.\n";
            return;
        }
        if (k == "size") {
            try {
                size = std::stoi(v);
            } catch (...) {
                std::cout << "[ERROR] size invalido.\n";
                return;
            }
        } else if (k == "unit") {
            unit = v;
        } else if (k == "path") {
            path = v;
        } else if (k == "type") {
            type = v;
        } else if (k == "fit") {
            fit = v;
        } else if (k == "name") {
            name = v;
        } else {
            std::cout << "[ERROR] Parametro no reconocido en FDISK: -" << k << "\n";
            return;
        }
    }

    if (size <= 0 || path.empty() || name.empty()) {
        std::cout << "[ERROR] FDISK requiere -size>0, -path y -name.\n";
        return;
    }

    int bytes = toBytes(size, unit);
    if (bytes <= 0) {
        std::cout << "[ERROR] -unit debe ser B, K o M.\n";
        return;
    }

    char ft = fitChar(fit);
    char tp = typeChar(type);
    if (ft == '?' || tp == '?') {
        std::cout << "[ERROR] Fit o type invalido en FDISK.\n";
        return;
    }

    SimDisk d;
    if (!cargarDisco(path, d)) {
        std::cout << "[ERROR] No se pudo abrir el disco .mia.\n";
        return;
    }

    for (auto& q : d.particiones) {
        if (q.part_name[0] && std::string(q.part_name) == name) {
            std::cout << "[ERROR] Ya existe una particion con ese nombre.\n";
            return;
        }
    }

    int ext = -1;
    buscarExtendida(d, ext);

    if (tp == 'e' && ext != -1) {
        std::cout << "[ERROR] Solo puede existir una particion extendida.\n";
        return;
    }

    if (tp == 'l') {
        if (ext == -1) {
            std::cout << "[ERROR] No existe una particion extendida.\n";
            return;
        }

        const auto& ep = d.particiones[ext];
        int epos = ep.part_start;
        int end = ep.part_start + ep.part_s;

        EBR cur{};
        if (!leerEBR(path, epos, cur)) {
            std::cout << "[ERROR] No se pudo leer el EBR inicial.\n";
            return;
        }

        int prev = epos;
        int curpos = epos;

        while (true) {
            if (cur.part_s == 0) {
                int data = curpos + int(sizeof(EBR));
                if (data + bytes > end) {
                    std::cout << "[ERROR] No hay espacio en la extendida.\n";
                    return;
                }

                EBR nuevo{};
                nuevo.part_status = '0';
                nuevo.part_fit = ft;
                nuevo.part_start = data;
                nuevo.part_s = bytes;
                nuevo.part_next = -1;
                std::strncpy(nuevo.part_name, name.c_str(), 15);
                nuevo.part_name[15] = 0;

                if (curpos == epos) {
                    if (!escribirEBR(path, curpos, nuevo)) {
                        std::cout << "[ERROR] No se pudo escribir EBR.\n";
                        return;
                    }
                } else {
                    if (!escribirEBR(path, curpos, nuevo)) {
                        std::cout << "[ERROR] No se pudo escribir EBR.\n";
                        return;
                    }
                }

                if (curpos != epos) {
                    EBR padre{};
                    leerEBR(path, prev, padre);
                    padre.part_next = curpos;
                    if (!escribirEBR(path, prev, padre)) {
                        std::cout << "[ERROR] No se pudo actualizar EBR anterior.\n";
                        return;
                    }
                }

                std::cout << "[EXITOSO] Particion logica creada fisicamente: " << name << "\n";
                return;
            }

            int siguiente = cur.part_next;
            int libreInicio = cur.part_start + cur.part_s;
            int libreFin = (siguiente == -1 ? end : siguiente);

            if (libreFin - libreInicio >= int(sizeof(EBR)) + bytes) {
                int npos = libreInicio;
                EBR nuevo{};
                nuevo.part_status = '0';
                nuevo.part_fit = ft;
                nuevo.part_start = npos + int(sizeof(EBR));
                nuevo.part_s = bytes;
                nuevo.part_next = siguiente;
                std::strncpy(nuevo.part_name, name.c_str(), 15);
                nuevo.part_name[15] = 0;

                cur.part_next = npos;
                if (!escribirEBR(path, curpos, cur) || !escribirEBR(path, npos, nuevo)) {
                    std::cout << "[ERROR] No se pudo escribir la particion logica.\n";
                    return;
                }

                std::cout << "[EXITOSO] Particion logica creada fisicamente: " << name << "\n";
                return;
            }

            if (siguiente == -1) break;
            prev = curpos;
            curpos = siguiente;

            if (!leerEBR(path, curpos, cur)) break;
        }

        std::cout << "[ERROR] No hay espacio disponible en la particion extendida.\n";
        return;
    }

    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        if (d.particiones[i].part_type != 'p' && d.particiones[i].part_type != 'e') {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        std::cout << "[ERROR] No hay slots libres en el MBR.\n";
        return;
    }

    std::vector<position> usados;
    for (int i = 0; i < 4; ++i) {
        if (d.particiones[i].part_type == 'p' || d.particiones[i].part_type == 'e') {
            position x{0, 0, 0, "", 0};
        }
    }

    // reconstruir espacios sin depender de objetos temporales del estado anterior
    for (int i = 0; i < 4; ++i) {
        if (d.particiones[i].part_type == 'p' || d.particiones[i].part_type == 'e') {
            usados.push_back({
                d.particiones[i].part_start,
                d.particiones[i].part_start + d.particiones[i].part_s - 1,
                d.particiones[i].part_type,
                std::string(d.particiones[i].part_name),
                d.particiones[i].part_s
            });
        }
    }

    std::sort(usados.begin(), usados.end());
    std::vector<libre> spaces;
    int cursor = EndMBR;

    for (auto& u : usados) {
        if (u.inicio > cursor) {
            spaces.push_back({cursor, u.inicio - cursor});
        }
        cursor = std::max(cursor, u.fin + 1);
    }

    if (cursor < d.tamano) {
        spaces.push_back({cursor, d.tamano - cursor});
    }

    int idx = choose(spaces, bytes, ft);
    if (idx < 0) {
        std::cout << "[ERROR] No hay espacio contiguo suficiente en el disco.\n";
        return;
    }

    particion np{};
    inicializarParticion(np);
    np.part_status = '0';
    np.part_type = tp;
    np.part_fit = ft;
    np.part_start = spaces[idx].inicio;
    np.part_s = bytes;
    np.part_correlativ = -1;
    std::strncpy(np.part_name, name.c_str(), 15);
    np.part_name[15] = 0;
    d.particiones[slot] = np;

    MBR m{};
    if (!leerMBR(path, m)) {
        std::cout << "[ERROR] No se pudo leer MBR.\n";
        return;
    }

    m.mbr_partition[slot] = np;
    if (!escribirMBR(path, m)) {
        std::cout << "[ERROR] No se pudo guardar la particion.\n";
        return;
    }

    estado.discos[path] = d;

    if (tp == 'e') {
        EBR e{};
        e.part_status = '0';
        e.part_fit = ft;
        e.part_start = np.part_start + int(sizeof(EBR));
        e.part_s = 0;
        e.part_next = -1;
        escribirEBR(path, np.part_start, e);
    }

    std::cout << "[EXITOSO] Particion " << (tp == 'e' ? "extendida" : "primaria") << " creada fisicamente: " << name << "\n";
}

void particionar(int size, char tipo, const std::string& ruta, const std::string& nombre, char fit, SimDisk& disco) {
    (void)size;
    (void)tipo;
    (void)ruta;
    (void)nombre;
    (void)fit;
    (void)disco;
}