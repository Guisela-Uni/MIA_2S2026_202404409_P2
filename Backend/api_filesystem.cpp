#include "api_filesystem.h"

#include "analizador.h"
#include "estado.h"
#include "ext2_utils.h"
#include "json_util.h"
#include "login.h"
#include "logout.h"
#include "journaling.h"

#include <algorithm>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string capturarSalida(const std::function<void()> &accion) {
    std::ostringstream salida;
    std::streambuf *anterior = std::cout.rdbuf(salida.rdbuf());
    try {
        accion();
    } catch (...) {
        std::cout.rdbuf(anterior);
        throw;
    }
    std::cout.rdbuf(anterior);
    return salida.str();
}

RespuestaHttp respuestaJson(int codigo, const std::string &cuerpo) {
    return RespuestaHttp{codigo, "application/json; charset=utf-8", cuerpo};
}

std::string jsonSesion(const EstadoGlobal &estado) {
    const auto &sesion = estado.sesion;
    std::ostringstream json;
    json << "{\"activa\":" << (sesion.activa ? "true" : "false");
    if (sesion.activa) {
        json << ",\"usuario\":\"" << escapar_json(sesion.usuario) << '\"'
             << ",\"id\":\"" << escapar_json(sesion.id) << '\"'
             << ",\"uid\":" << sesion.uid
             << ",\"gid\":" << sesion.gid;
    }
    json << '}';
    return json.str();
}

std::string unirRuta(const std::string &base, const std::string &nombre) {
    return base == "/" ? "/" + nombre : base + "/" + nombre;
}

std::string fechaHora(float marcaTiempo) {
    const std::time_t tiempo = static_cast<std::time_t>(marcaTiempo);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &tiempo) != 0) return "";
#else
    if (localtime_r(&tiempo, &local) == nullptr) return "";
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) return "";
    return buffer;
}

bool obtenerSesionContexto(EstadoGlobal &estado, Ext2Context &ctx) {
    if (!estado.sesion.activa) return false;
    return obtenerContexto(estado, estado.sesion.id, ctx);
}

} // namespace

RespuestaHttp manejarHealthHttp(const PeticionHttp &) {
    return respuestaJson(200, R"({"ok":true,"servicio":"C++ DISK ONLINE"})");
}

RespuestaHttp manejarLoginHttp(const PeticionHttp &peticion) {
    const std::string usuario = extraer_campo_json(peticion.cuerpo, "user");
    const std::string password = extraer_campo_json(peticion.cuerpo, "password");
    const std::string id = extraer_campo_json(peticion.cuerpo, "id");

    if (usuario.empty() || password.empty() || id.empty()) {
        return respuestaJson(400, R"({"ok":false,"error":"Se requieren user, password e id."})");
    }

    EstadoGlobal &estado = obtener_estado();
    const std::vector<std::string> parametros = {
        "login", "user=" + usuario, "pass=" + password, "id=" + id
    };
    const std::string resultado = capturarSalida([&]() {
        std::vector<std::string> copia = parametros;
        login(copia, estado);
    });

    const bool correcto = resultado.find("[EXITOSO]") != std::string::npos && estado.sesion.activa;
    std::ostringstream json;
    json << "{\"ok\":" << (correcto ? "true" : "false")
         << ",\"resultado\":\"" << escapar_json(resultado) << '\"'
         << ",\"sesion\":" << jsonSesion(estado) << '}';
    return respuestaJson(correcto ? 200 : 401, json.str());
}

RespuestaHttp manejarLogoutHttp(const PeticionHttp &) {
    EstadoGlobal &estado = obtener_estado();

    // LOSS invalida deliberadamente la sesion porque elimina las estructuras del
    // filesystem. Permitir logout idempotente evita que la interfaz muestre un
    // error si el usuario pulsa "Cerrar sesión" después de ejecutar LOSS.
    if (!estado.sesion.activa) {
        return respuestaJson(200,
            "{\"ok\":true,\"resultado\":\"[INFO] No habia una sesion activa; la sesion ya estaba cerrada.\",\"sesion\":"
            + jsonSesion(estado) + "}");
    }

    const std::string resultado = capturarSalida([&]() {
        std::vector<std::string> parametros = {"logout"};
        logout(parametros, estado);
    });
    const bool correcto = resultado.find("[EXITOSO]") != std::string::npos;
    std::ostringstream json;
    json << "{\"ok\":" << (correcto ? "true" : "false")
         << ",\"resultado\":\"" << escapar_json(resultado) << '\"'
         << ",\"sesion\":" << jsonSesion(estado) << '}';
    return respuestaJson(correcto ? 200 : 400, json.str());
}

RespuestaHttp manejarSesionHttp(const PeticionHttp &) {
    EstadoGlobal &estado = obtener_estado();
    return respuestaJson(200, "{\"ok\":true,\"sesion\":" + jsonSesion(estado) + "}");
}

RespuestaHttp manejarListarDirectorioHttp(const PeticionHttp &peticion) {
    const std::string path = extraer_campo_json(peticion.cuerpo, "path");
    if (path.empty()) {
        return respuestaJson(400, R"({"error":"Se requiere path."})");
    }

    EstadoGlobal &estado = obtener_estado();
    Ext2Context ctx{};
    if (!obtenerSesionContexto(estado, ctx)) {
        return respuestaJson(401, R"({"error":"No existe una sesion activa."})");
    }

    const std::string ruta = normalizarRuta(path);
    int indiceDirectorio = -1;
    if (!resolverRuta(ctx, ruta, indiceDirectorio)) {
        return respuestaJson(404, R"({"error":"La ruta no existe."})");
    }

    inodo directorio{};
    if (!leerInodo(ctx, indiceDirectorio, directorio) || directorio.i_type != 0) {
        return respuestaJson(400, R"({"error":"La ruta no corresponde a un directorio."})");
    }
    if (!puedeLeer(directorio, estado.sesion.uid, estado.sesion.gid)) {
        return respuestaJson(403, R"({"error":"No tiene permiso para leer este directorio."})");
    }

    std::vector<std::pair<std::string, int>> entradas;
    if (!listarEntradasDirectorio(ctx, indiceDirectorio, entradas)) {
        return respuestaJson(500, R"({"error":"No se pudo leer el directorio."})");
    }

    std::ostringstream json;
    json << "{\"path\":\"" << escapar_json(ruta) << "\",\"items\":[";
    bool primero = true;
    for (const auto &entrada : entradas) {
        if (entrada.first == "." || entrada.first == "..") continue;
        inodo hijo{};
        if (!leerInodo(ctx, entrada.second, hijo)) continue;

        if (!primero) json << ',';
        primero = false;
        const std::string rutaHijo = unirRuta(ruta, entrada.first);
        const bool esDirectorio = hijo.i_type == 0;
        json << "{\"name\":\"" << escapar_json(entrada.first) << '\"'
             << ",\"path\":\"" << escapar_json(rutaHijo) << '\"'
             << ",\"type\":\"" << (esDirectorio ? "directory" : "file") << '\"'
             << ",\"size\":" << hijo.i_s
             << ",\"permissions\":" << hijo.i_perm
             << ",\"uid\":" << hijo.i_uid
             << ",\"gid\":" << hijo.i_gid
             << ",\"readable\":" << (puedeLeer(hijo, estado.sesion.uid, estado.sesion.gid) ? "true" : "false")
             << ",\"writable\":" << (puedeEscribir(hijo, estado.sesion.uid, estado.sesion.gid) ? "true" : "false")
             << '}';
    }
    json << "]}";
    return respuestaJson(200, json.str());
}

RespuestaHttp manejarLeerArchivoHttp(const PeticionHttp &peticion) {
    const std::string path = extraer_campo_json(peticion.cuerpo, "path");
    if (path.empty()) {
        return respuestaJson(400, R"({"error":"Se requiere path."})");
    }

    EstadoGlobal &estado = obtener_estado();
    Ext2Context ctx{};
    if (!obtenerSesionContexto(estado, ctx)) {
        return respuestaJson(401, R"({"error":"No existe una sesion activa."})");
    }

    const std::string ruta = normalizarRuta(path);
    int indiceArchivo = -1;
    if (!resolverRuta(ctx, ruta, indiceArchivo)) {
        return respuestaJson(404, R"({"error":"El archivo no existe."})");
    }

    inodo archivo{};
    if (!leerInodo(ctx, indiceArchivo, archivo) || archivo.i_type != 1) {
        return respuestaJson(400, R"({"error":"La ruta no corresponde a un archivo."})");
    }
    if (!puedeLeer(archivo, estado.sesion.uid, estado.sesion.gid)) {
        return respuestaJson(403, R"({"error":"No tiene permiso para leer este archivo."})");
    }

    const std::string contenido = leerArchivo(ctx, archivo);
    std::ostringstream json;
    json << "{\"path\":\"" << escapar_json(ruta) << '\"'
         << ",\"content\":\"" << escapar_json(contenido) << '\"'
         << ",\"size\":" << archivo.i_s
         << ",\"permissions\":" << archivo.i_perm
         << ",\"uid\":" << archivo.i_uid
         << ",\"gid\":" << archivo.i_gid << '}';
    return respuestaJson(200, json.str());
}

RespuestaHttp manejarJournalHttp(const PeticionHttp &peticion) {
    std::string id = extraer_campo_json(peticion.cuerpo, "id");
    EstadoGlobal &estado = obtener_estado();
    if (!estado.sesion.activa) {
        return respuestaJson(401, R"({"error":"Debe iniciar sesion para consultar el journaling."})");
    }
    if (id.empty()) id = estado.sesion.id;
    if (id != estado.sesion.id) {
        return respuestaJson(403, R"({"error":"Solo puede consultar el journaling de la particion de su sesion."})");
    }

    Ext2Context ctx{};
    if (!obtenerContexto(estado, id, ctx)) {
        return respuestaJson(404, R"({"error":"La particion no esta montada o no contiene un filesystem valido."})");
    }
    if (!esExt3(ctx)) {
        return respuestaJson(400, R"({"error":"La particion no esta formateada como EXT3."})");
    }

    const auto entradas = leerJournal(ctx);
    std::ostringstream json;
    json << "{\"id\":\"" << escapar_json(id) << "\",\"entries\":[";
    for (std::size_t i = 0; i < entradas.size(); ++i) {
        if (i) json << ',';
        const auto &registro = entradas[i];
        json << "{\"count\":" << registro.j_count
             << ",\"operation\":\"" << escapar_json(registro.j_content.i_operation) << '\"'
             << ",\"path\":\"" << escapar_json(registro.j_content.i_path) << '\"'
             << ",\"content\":\"" << escapar_json(registro.j_content.i_content) << '\"'
             << ",\"timestamp\":" << registro.j_content.i_date
             << ",\"datetime\":\"" << escapar_json(fechaHora(registro.j_content.i_date)) << "\"}";
    }
    json << "]}";
    return respuestaJson(200, json.str());
}
