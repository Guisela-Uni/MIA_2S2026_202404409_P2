#include "mkfile.h"
#include "ext2_utils.h"
#include "journaling.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
namespace {

std::string low(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool split(const std::string& t, std::string& k, std::string& v) {
    auto p = t.find('=');
    if (p == std::string::npos) {
        return false;
    }
    
    k = low(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

std::string parent(const std::string& p) {
    auto n = p.find_last_of('/');
    return n <= 0 ? "/" : p.substr(0, n);
}

std::string base(const std::string& p) {
    auto n = p.find_last_of('/');
    return n == std::string::npos ? p : p.substr(n + 1);
}

} // namespace

void mkfile(std::vector<std::string>& p, EstadoGlobal& e) {
    std::string path, cont;
    bool r = false, hasSize = false, sobrescribir = false;
    long long size = 0;
    
    for (size_t i = 1; i < p.size(); i++) {
        std::string k, v;
        
        if (p[i].find('=') == std::string::npos) {
            if (low(p[i]) == "r") {
                r = true;
                continue;
            }
            std::cout << "[ERROR] MKFILE: parametro invalido " << p[i] << "\n";
            return;
        }
        
        if (!split(p[i], k, v)) {
            return;
        }
        
        if (k == "path") {
            path = v;
        } else if (k == "size") {
            hasSize = true;
            try {
                size = std::stoll(v);
            } catch (...) {
                std::cout << "[ERROR] -size debe ser numerico.\n";
                return;
            }
        } else if (k == "cont") {
            cont = v;
        } else if (k == "overwrite") {
            const std::string valor = low(v);
            if (valor == "true" || valor == "1" || valor == "si") {
                sobrescribir = true;
            } else if (valor == "false" || valor == "0" || valor == "no") {
                sobrescribir = false;
            } else {
                std::cout << "[ERROR] -overwrite acepta true o false.\n";
                return;
            }
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MKFILE: -" << k << "\n";
            return;
        }
    }
    
    if (path.empty()) {
        std::cout << "[ERROR] MKFILE requiere -path.\n";
        return;
    }
    
    if (size < 0) {
        std::cout << "[ERROR] -size no puede ser negativo.\n";
        return;
    }
    
    Ext2Context c;
    
    if (!sesionActiva(e, c)) {
        std::cout << "[ERROR] No existe una sesion activa.\n";
        return;
    }
    
    std::string pp = parent(path), name = base(path);
    
    if (name.empty() || name.size() > sizeof(content::b_name)) {
        std::cout << "[ERROR] El nombre del archivo debe tener entre 1 y 12 caracteres por la estructura de directorio del proyecto.\n";
        return;
    }
    
    int padre;
    
    if (!resolverRuta(c, pp, padre)) {
        if (!r) {
            std::cout << "[ERROR] No existe la carpeta padre. Use -r.\n";
            return;
        }
        
        std::vector<std::string> parts = partesRuta(pp);
        padre = 0;
        
        for (auto& x : parts) {
            int n;
            
            if (buscarEntrada(c, padre, x, n)) {
                inodo q{};
                leerInodo(c, n, q);
                
                if (q.i_type != 0) {
                    std::cout << "[ERROR] Una parte de la ruta no es carpeta.\n";
                    return;
                }
                
                padre = n;
                continue;
            }
            
            inodo pi{};
            leerInodo(c, padre, pi);
            
            if (!puedeEscribir(pi, e.sesion.uid, e.sesion.gid)) {
                std::cout << "[ERROR] No tiene permiso para crear carpetas.\n";
                return;
            }
            
            if (!crearDirectorio(c, padre, x, e.sesion.uid, e.sesion.gid, n)) {
                std::cout << "[ERROR] No se pudo crear una carpeta intermedia.\n";
                return;
            }
            
            padre = n;
        }
    }
    
    int old = -1;
    
    if (buscarEntrada(c, padre, name, old)) {
        inodo oi{};
        leerInodo(c, old, oi);
        
        if (oi.i_type != 1) {
            std::cout << "[ERROR] Ya existe una carpeta con ese nombre.\n";
            return;
        }
        
        if (!sobrescribir) {
#if defined(API_MODE)
            std::cout << "[CONFIRMACION] El archivo ya existe. Para sobrescribir desde la interfaz web, "
                         "confirme y vuelva a enviar el comando agregando -overwrite=true.\n";
            return;
#else
            std::cout << "[AVISO] El archivo ya existe. ¿Desea sobreescribirlo? (s/n): ";
            std::string ans;
            std::getline(std::cin, ans);
            if (low(ans) != "s" && low(ans) != "si") {
                std::cout << "[INFO] Operacion cancelada.\n";
                return;
            }
#endif
        }

        std::string data;
        
        if (!cont.empty()) {
            std::ifstream f(cont, std::ios::binary);
            
            if (!f) {
                std::cout << "[ERROR] El archivo indicado por -cont no existe o no puede leerse.\n";
                return;
            }
            
            data.assign((std::istreambuf_iterator<char>(f)), {});
        } else if (hasSize) {
            data.resize((size_t)size);
            for (size_t i = 0; i < data.size(); i++) {
                data[i] = char('0' + (i % 10));
            }
        }
        
        if (!escribirArchivoCompleto(c, old, data)) {
            std::cout << "[ERROR] No se pudo escribir el contenido.\n";
            return;
        }
        
        if (esExt3(c) && !registrarJournal(c, "overwrite", normalizarRuta(path), data)) {
            std::cout << "[AVISO] El archivo se sobrescribio, pero no se pudo registrar en el journaling.\n";
        }

        std::cout << "[EXITOSO] Archivo sobrescrito: " << path << "\n";
        return;
    }
    
    inodo pi{};
    
    if (!leerInodo(c, padre, pi) || !puedeEscribir(pi, e.sesion.uid, e.sesion.gid)) {
        std::cout << "[ERROR] No tiene permiso de escritura en la carpeta padre.\n";
        return;
    }
    
    std::string data;
    
    if (!cont.empty()) {
        std::ifstream f(cont, std::ios::binary);
        
        if (!f) {
            std::cout << "[ERROR] El archivo indicado por -cont no existe o no puede leerse.\n";
            return;
        }
        
        data.assign((std::istreambuf_iterator<char>(f)), {});
    } else if (hasSize) {
        data.resize((size_t)size);
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = char('0' + (i % 10));
        }
    }
    
    int n;
    
    if (!crearArchivo(c, padre, name, data, e.sesion.uid, e.sesion.gid, n)) {
        std::cout << "[ERROR] No se pudo crear el archivo.\n";
        return;
    }
    
    if (esExt3(c) && !registrarJournal(c, "mkfile", normalizarRuta(path), data)) {
        std::cout << "[AVISO] El archivo se creo, pero no se pudo registrar en el journaling.\n";
    }

    std::cout << "[EXITOSO] Archivo creado fisicamente: " << path
              << " (" << data.size() << " bytes)\n";
}