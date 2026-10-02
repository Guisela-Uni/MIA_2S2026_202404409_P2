#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cctype>

#include "analizador.h"
#include "estado.h"
#include "disk_utils.h"
#include "http_server.h"
#include "json_util.h"

namespace {
RespuestaHttp manejarComando(const PeticionHttp &peticion) {
    std::string comando = extraer_campo_json(peticion.cuerpo, "comando");

    if (comando.empty()) {
        RespuestaHttp respuesta;
        respuesta.codigo = 400;
        respuesta.cuerpo = R"({"error":"falta el campo 'comando' en el JSON"})";
        return respuesta;
    }

    // Aca se conecta con TODO el backend que ya existia: el mismo lexer/analizador que usa la consola.
    std::string resultado = ejecutar_comando_texto(comando);

    RespuestaHttp respuesta;
    respuesta.codigo = 200;
    respuesta.cuerpo = "{\"resultado\":\"" + escapar_json(resultado) + "\"}";
    return respuesta;
}


RespuestaHttp manejarProcesarArchivo(const PeticionHttp &peticion) {
    std::string contenido = extraer_campo_json(peticion.cuerpo, "contenido");

    if (contenido.empty()) {
        RespuestaHttp respuesta;
        respuesta.codigo = 400;
        respuesta.cuerpo = R"({"error":"falta el campo 'contenido' en el JSON"})";
        return respuesta;
    }

    std::string reporte = procesar_archivo_comandos(contenido);

    RespuestaHttp respuesta;
    respuesta.codigo = 200;
    respuesta.cuerpo = "{\"reporte\":\"" + escapar_json(reporte) + "\"}";
    return respuesta;
}


std::string nombreSeguro(const std::string& nombre) {
    std::string r;
    for (char c : nombre) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c=='.' || c=='_' || c=='-') r += c;
    }
    return r;
}

RespuestaHttp manejarSubirMia(const PeticionHttp &peticion) {
    if (peticion.cuerpo.empty()) {
        return RespuestaHttp{400, "application/json", R"({"error":"el archivo .mia esta vacio"})"};
    }
    std::string nombre = "disco_subido.mia";
    auto qpos = peticion.ruta.find("nombre=");
    if (qpos != std::string::npos) {
        nombre = nombreSeguro(peticion.ruta.substr(qpos + 7));
        if (nombre.empty()) nombre = "disco_subido.mia";
    }
    if (nombre.size() < 4 || nombre.substr(nombre.size()-4) != ".mia") {
        return RespuestaHttp{400, "application/json", R"({"error":"solo se permiten archivos .mia"})"};
    }
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "analizador_ext2_uploads";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return RespuestaHttp{500, "application/json", R"({"error":"no se pudo crear la carpeta de archivos subidos"})"};
    std::filesystem::path destino = dir / nombre;
    std::ofstream f(destino, std::ios::binary | std::ios::trunc);
    if (!f) return RespuestaHttp{500, "application/json", R"({"error":"no se pudo guardar el archivo .mia"})"};
    f.write(peticion.cuerpo.data(), static_cast<std::streamsize>(peticion.cuerpo.size()));
    f.close();
    SimDisk d;
    if (!cargarDisco(destino.string(), d)) {
        std::filesystem::remove(destino, ec);
        return RespuestaHttp{400, "application/json", R"({"error":"el archivo no contiene un MBR valido"})"};
    }
    obtener_estado().discos[destino.string()] = d;
    std::string json = "{\"nombre\":\"" + escapar_json(nombre) + "\",\"ruta\":\"" + escapar_json(destino.string()) + "\",\"tamano\":" + std::to_string(d.tamano) + "}";
    return RespuestaHttp{200, "application/json", json};
}

RespuestaHttp manejarEstado(const PeticionHttp & /*peticion*/) {
    EstadoGlobal &estado = obtener_estado();
    std::ostringstream json;

    json << "{";

    //  discos simulados (creados con MKDISK, aunque no esten montados)
    json << "\"discos\":[";
    bool primerDisco = true;
    for (const auto &[path, disco] : estado.discos) {
        if (!primerDisco) json << ",";
        primerDisco = false;
        json << "{\"path\":\"" << escapar_json(path) << "\","
             << "\"tamano\":" << disco.tamano << ","
             << "\"fit\":\"" << disco.fit << "\"}";
    }
    json << "],";

    //  particiones montadas (con su id, generado por MOUNT)
    json << "\"montadas\":[";
    bool primeraMontada = true;
    for (const auto &d : estado.discosMontados) {
        for (const auto &p : d.particiones) {
            if (!primeraMontada) json << ",";
            primeraMontada = false;
            json << "{\"id\":\"" << escapar_json(p.id) << "\","
                 << "\"disco\":\"" << escapar_json(d.nombre) << "\","
                 << "\"particion\":\"" << escapar_json(p.nombre) << "\","
                 << "\"size\":" << p.size << ","
                 << "\"formateada\":" << (estado.particionesFormateadas.count(p.id) ? "true" : "false")
                 << "}";
        }
    }
    json << "],";

    //  usuarios simulados (MKUSR/RMUSR)
    json << "\"usuarios\":[";
    bool primerUsuario = true;
    for (const auto &u : estado.usuarios) {
        if (!primerUsuario) json << ",";
        primerUsuario = false;
        json << "{\"user\":\"" << escapar_json(u.user) << "\","
             << "\"grupo\":\"" << escapar_json(u.grupo) << "\"}";
    }
    json << "],";

    //  archivos simulados (MKFILE)
    json << "\"archivos\":[";
    bool primerArchivo = true;
    for (const auto &[path, archivo] : estado.archivos) {
        if (!primerArchivo) json << ",";
        primerArchivo = false;
        json << "{\"path\":\"" << escapar_json(path) << "\","
             << "\"size\":" << archivo.size << "}";
    }
    json << "]";

    json << "}";

    RespuestaHttp respuesta;
    respuesta.codigo = 200;
    respuesta.cuerpo = json.str();
    return respuesta;
}

}  // namespace

int main() {
    const int PUERTO = 8080;

    ServidorHttp servidor;
    servidor.agregarRuta("POST", "/api/comando", manejarComando);
    servidor.agregarRuta("POST", "/api/procesar", manejarProcesarArchivo);
    servidor.agregarRuta("POST", "/api/subir-mia", manejarSubirMia);
    servidor.agregarRuta("GET", "/api/estado", manejarEstado);

    std::cout << "======================================================\n";
    std::cout << " Analizador EXT2 - API REST (backend para el frontend)\n";
    std::cout << "======================================================\n";
    std::cout << "POST http://localhost:" << PUERTO << "/api/comando   {\"comando\": \"...\"}\n";
    std::cout << "POST http://localhost:" << PUERTO << "/api/procesar  {\"contenido\": \"varias\\nlineas\"}\n";
    std::cout << "GET  http://localhost:" << PUERTO << "/api/estado\n\n";

    servidor.escuchar(PUERTO);  // se queda corriendo para siempre
    return 0;
}