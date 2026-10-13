#include <iostream>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <cstdlib>

#include "analizador.h"
#include "estado.h"
#include "disk_utils.h"
#include "ext2_utils.h"
#include "http_server.h"
#include "json_util.h"
#include "api_filesystem.h"

namespace {
std::string minusculas(std::string valor) {
    std::transform(valor.begin(), valor.end(), valor.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return valor;
}

bool contieneComandoSesionDirecto(const std::string &contenido) {
    std::istringstream lineas(contenido);
    std::string linea;
    while (std::getline(lineas, linea)) {
        const auto inicio = linea.find_first_not_of(" \t\r");
        if (inicio == std::string::npos || linea[inicio] == '#') continue;
        std::istringstream tokens(linea.substr(inicio));
        std::string primerToken;
        tokens >> primerToken;
        primerToken = minusculas(primerToken);
        if (primerToken == "login" || primerToken == "logout") return true;
    }
    return false;
}

RespuestaHttp manejarComando(const PeticionHttp &peticion) {
    std::string comando = extraer_campo_json(peticion.cuerpo, "comando");

    if (comando.empty()) {
        RespuestaHttp respuesta;
        respuesta.codigo = 400;
        respuesta.cuerpo = R"({"error":"falta el campo 'comando' en el JSON"})";
        return respuesta;
    }

    if (contieneComandoSesionDirecto(comando)) {
        RespuestaHttp respuesta;
        respuesta.codigo = 400;
        respuesta.cuerpo = R"({"error":"En la API, el inicio y cierre de sesion se realizan desde la interfaz grafica mediante /api/login y /api/logout."})";
        return respuesta;
    }

    // El terminal ejecuta el analizador compartido, pero el login web se controla
    // mediante endpoints dedicados para mantener sincronizado el estado de React.
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

    if (contieneComandoSesionDirecto(contenido)) {
        RespuestaHttp respuesta;
        respuesta.codigo = 400;
        respuesta.cuerpo = R"({"error":"Los archivos de comandos no pueden incluir LOGIN o LOGOUT desde la API; utilice la interfaz grafica."})";
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
        std::string parametro = peticion.ruta.substr(qpos + 7);
        const std::size_t siguiente = parametro.find('&');
        if (siguiente != std::string::npos) parametro.resize(siguiente);
        nombre = nombreSeguro(parametro);
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

namespace {

std::string textoAcotado(const char *texto, std::size_t capacidad) {
    std::size_t longitud = 0;
    while (longitud < capacidad && texto[longitud] != '\0') ++longitud;
    return std::string(texto, longitud);
}

const montada *buscarMontajePorRutaNombre(const EstadoGlobal &estado,
                                           const std::string &ruta,
                                           const std::string &nombre) {
    for (const auto &d : estado.discosMontados) {
        if (d.ruta != ruta) continue;
        for (const auto &p : d.particiones) {
            if (p.nombre == nombre) return &p;
        }
    }
    return nullptr;
}

std::string jsonParticionInventario(EstadoGlobal &estado,
                                    const std::string &nombre,
                                    int inicio,
                                    int tamano,
                                    char tipo,
                                    char fit,
                                    const montada *montaje,
                                    const std::string &contenedora = "") {
    const bool montada = montaje != nullptr;
    bool formateada = false;
    int tipoFs = 0;
    int inodos = 0;
    int inodosLibres = 0;
    int bloques = 0;
    int bloquesLibres = 0;

    if (montaje) {
        Ext2Context contexto{};
        if (obtenerContexto(estado, montaje->id, contexto) &&
            contexto.sb.s_magic == EXT2_MAGIC &&
            (contexto.sb.s_filesystem_type == 2 || contexto.sb.s_filesystem_type == 3) &&
            contexto.sb.s_inodes_count > 0 && contexto.sb.s_blocks_count > 0) {
            formateada = true;
            tipoFs = contexto.sb.s_filesystem_type;
            inodos = contexto.sb.s_inodes_count;
            inodosLibres = contexto.sb.s_free_inodes_count;
            bloques = contexto.sb.s_blocks_count;
            bloquesLibres = contexto.sb.s_free_blocks_count;
        }
    }

    std::ostringstream json;
    json << "{\"nombre\":\"" << escapar_json(nombre) << '\"'
         << ",\"inicio\":" << inicio
         << ",\"size\":" << tamano
         << ",\"tipo\":\"" << tipo << '\"'
         << ",\"tipoNombre\":\""
         << (tipo == 'p' ? "Primaria" : tipo == 'e' ? "Extendida" : "Logica") << '\"'
         << ",\"fit\":\"" << fit << '\"'
         << ",\"contenedora\":\"" << escapar_json(contenedora) << '\"'
         << ",\"montada\":" << (montada ? "true" : "false")
         << ",\"id\":\"" << (montaje ? escapar_json(montaje->id) : "") << '\"'
         << ",\"formateada\":" << (formateada ? "true" : "false")
         << ",\"filesystemType\":" << tipoFs
         << ",\"filesystem\":\""
         << (tipoFs == 3 ? "EXT3" : tipoFs == 2 ? "EXT2" : "SIN FORMATO") << '\"'
         << ",\"inodos\":" << inodos
         << ",\"inodosLibres\":" << inodosLibres
         << ",\"bloques\":" << bloques
         << ",\"bloquesLibres\":" << bloquesLibres
         << ",\"estado\":\""
         << (tipo == 'e' ? "Contenedora" : montada ? (formateada ? "Montada y formateada" : "Montada sin formato") : "Sin montar")
         << "\"}";
    return json.str();
}

std::filesystem::path rutaRegistroDiscos() {
    if (const char *configurada = std::getenv("DISK_ONLINE_REGISTRY")) {
        if (*configurada != '\0') return std::filesystem::path(configurada);
    }
    return std::filesystem::current_path() / ".mia_registry";
}

void cargarYGuardarInventario(EstadoGlobal &estado) {
    const std::filesystem::path registro = rutaRegistroDiscos();
    std::error_code ec;
    if (registro.has_parent_path()) {
        std::filesystem::create_directories(registro.parent_path(), ec);
    }

    std::ifstream entrada(registro);
    std::string rutaGuardada;
    while (std::getline(entrada, rutaGuardada)) {
        if (rutaGuardada.empty()) continue;
        std::error_code rutaError;
        const auto guardadaNormalizada = std::filesystem::absolute(rutaGuardada, rutaError).lexically_normal();
        bool yaConocida = false;
        for (const auto &[rutaConocida, discoConocido] : estado.discos) {
            (void)discoConocido;
            std::error_code conocidaError;
            const auto conocidaNormalizada = std::filesystem::absolute(rutaConocida, conocidaError).lexically_normal();
            if (!rutaError && !conocidaError && guardadaNormalizada == conocidaNormalizada) {
                yaConocida = true;
                break;
            }
            if (rutaConocida == rutaGuardada) {
                yaConocida = true;
                break;
            }
        }
        if (yaConocida) continue;
        SimDisk disco{};
        if (cargarDisco(rutaGuardada, disco)) estado.discos[rutaGuardada] = disco;
    }

    // La lista solo conserva archivos .mia que todavía contienen un MBR legible.
    for (auto it = estado.discos.begin(); it != estado.discos.end();) {
        SimDisk actualizado{};
        if (!cargarDisco(it->first, actualizado)) {
            it = estado.discos.erase(it);
            continue;
        }
        it->second = actualizado;
        ++it;
    }

    std::ofstream salida(registro, std::ios::trunc);
    if (!salida) return; // La API sigue funcionando aunque el registro no sea escribible.
    std::set<std::filesystem::path> rutasUnicas;
    for (const auto &[ruta, disco] : estado.discos) {
        (void)disco;
        std::filesystem::path absoluta = std::filesystem::absolute(ruta, ec).lexically_normal();
        if (ec) {
            ec.clear();
            absoluta = std::filesystem::path(ruta).lexically_normal();
        }
        if (rutasUnicas.insert(absoluta).second) salida << absoluta.string() << '\n';
    }
}

std::string jsonParticionesDisco(EstadoGlobal &estado, const std::string &ruta,
                                 const SimDisk &disco) {
    std::ostringstream json;
    json << '[';
    bool primera = true;
    for (int i = 0; i < 4; ++i) {
        const particion &p = disco.particiones[i];
        if (p.part_s <= 0 || (p.part_type != 'p' && p.part_type != 'e')) continue;
        const std::string nombre = textoAcotado(p.part_name, sizeof(p.part_name));
        if (nombre.empty()) continue;
        const montada *montaje = buscarMontajePorRutaNombre(estado, ruta, nombre);
        if (!primera) json << ',';
        primera = false;
        json << jsonParticionInventario(estado, nombre, p.part_start,
                                        p.part_s, p.part_type, p.part_fit, montaje);

        if (p.part_type != 'e') continue;
        const int limite = p.part_start + p.part_s;
        int offset = p.part_start;
        int proteccionCiclos = 0;
        while (offset >= p.part_start && offset + static_cast<int>(sizeof(EBR)) <= limite &&
               proteccionCiclos++ < 4096) {
            EBR ebr{};
            if (!leerEBR(ruta, offset, ebr)) break;
            const std::string nombreLogico = textoAcotado(ebr.part_name, sizeof(ebr.part_name));
            if (ebr.part_s > 0 && !nombreLogico.empty()) {
                const montada *montajeLogico = buscarMontajePorRutaNombre(estado, ruta, nombreLogico);
                if (!primera) json << ',';
                primera = false;
                json << jsonParticionInventario(estado, nombreLogico,
                                                ebr.part_start, ebr.part_s,
                                                'l', ebr.part_fit, montajeLogico, nombre);
            }
            if (ebr.part_next == -1 || ebr.part_next <= offset || ebr.part_next >= limite) break;
            offset = ebr.part_next;
        }
    }
    json << ']';
    return json.str();
}

} // namespace

RespuestaHttp manejarEstado(const PeticionHttp & /*peticion*/) {
    EstadoGlobal &estado = obtener_estado();
    cargarYGuardarInventario(estado);
    std::ostringstream json;

    json << "{";

    //  discos simulados (creados con MKDISK, aunque no esten montados)
    json << "\"discos\":[";
    bool primerDisco = true;
    for (const auto &[path, discoCache] : estado.discos) {
        SimDisk discoActual = discoCache;
        // El archivo .mia es la fuente persistente; refrescamos el MBR al consultar.
        cargarDisco(path, discoActual);
        if (!primerDisco) json << ",";
        primerDisco = false;
        const std::string particiones = jsonParticionesDisco(estado, path, discoActual);
        int cantidadMontadas = 0;
        for (const auto &montaje : estado.discosMontados) {
            if (montaje.ruta != path) continue;
            cantidadMontadas += static_cast<int>(montaje.particiones.size());
        }
        json << "{\"path\":\"" << escapar_json(path) << "\","
             << "\"nombre\":\"" << escapar_json(nombreDisco(path)) << "\","
             << "\"tamano\":" << discoActual.tamano << ","
             << "\"fit\":\"" << discoActual.fit << "\","
             << "\"fechaCreacion\":" << static_cast<long long>(discoActual.creado) << ","
             << "\"firma\":" << discoActual.signature << ","
             << "\"particionesMontadas\":" << cantidadMontadas << ","
             << "\"particiones\":" << particiones << "}";
    }
    json << "],";

    //  particiones montadas (con su id, generado por MOUNT)
    json << "\"montadas\":[";
    bool primeraMontada = true;
    for (const auto &d : estado.discosMontados) {
        for (const auto &p : d.particiones) {
            if (!primeraMontada) json << ",";
            primeraMontada = false;
            const std::string claveFormato = d.ruta + "|" + p.nombre;
            Ext2Context contexto{};
            const bool contextoValido = obtenerContexto(estado, p.id, contexto) &&
                contexto.sb.s_magic == EXT2_MAGIC &&
                (contexto.sb.s_filesystem_type == 2 || contexto.sb.s_filesystem_type == 3) &&
                contexto.sb.s_inodes_count > 0 && contexto.sb.s_blocks_count > 0;

            // El superbloque físico es la fuente de verdad. La caché solo puede
            // invalidar una marca, nunca afirmar que un filesystem existe si no
            // es posible validar la estructura sobre el disco.
            const auto marcaFormato = estado.particionesFormateadas.find(claveFormato);
            const bool formateada = contextoValido &&
                (marcaFormato == estado.particionesFormateadas.end() || marcaFormato->second);
            const int tipoFilesystem = formateada ? contexto.sb.s_filesystem_type : 0;
            json << "{\"id\":\"" << escapar_json(p.id) << "\","
                 << "\"disco\":\"" << escapar_json(d.nombre) << "\","
                 << "\"rutaDisco\":\"" << escapar_json(d.ruta) << "\","
                 << "\"particion\":\"" << escapar_json(p.nombre) << "\","
                 << "\"size\":" << p.size << ","
                 << "\"montada\":true,"
                 << "\"formateada\":" << (formateada ? "true" : "false")
                 << ",\"filesystemType\":" << tipoFilesystem
                 << ",\"filesystem\":\""
                 << (tipoFilesystem == 3 ? "EXT3" : (tipoFilesystem == 2 ? "EXT2" : "SIN FORMATO"))
                 << "\"";
            SimDisk discoInfo{};
            char fitParticion = '?';
            char tipoParticion = '?';
            if (cargarDisco(d.ruta, discoInfo)) {
                if (p.posMBR >= 0 && p.posMBR < 4) {
                    fitParticion = discoInfo.particiones[p.posMBR].part_fit;
                    tipoParticion = discoInfo.particiones[p.posMBR].part_type;
                } else if (p.posEBR >= 0) {
                    EBR ebr{};
                    if (leerEBR(d.ruta, p.posEBR, ebr)) fitParticion = ebr.part_fit;
                    tipoParticion = 'l';
                }
            }
            json << ",\"fit\":\"" << fitParticion << "\","
                 << "\"tipo\":\"" << tipoParticion << "\"}";
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
    int puerto = 8080;
    if (const char *valorPuerto = std::getenv("PORT")) {
        try {
            const int candidato = std::stoi(valorPuerto);
            if (candidato > 0 && candidato <= 65535) puerto = candidato;
        } catch (...) {
            std::cerr << "[AVISO] PORT invalido; se utilizara 8080.\n";
        }
    }

    ServidorHttp servidor;
    servidor.agregarRuta("POST", "/api/comando", manejarComando);
    servidor.agregarRuta("POST", "/api/procesar", manejarProcesarArchivo);
    servidor.agregarRuta("POST", "/api/subir-mia", manejarSubirMia);
    servidor.agregarRuta("GET", "/api/estado", manejarEstado);
    servidor.agregarRuta("GET", "/api/health", manejarHealthHttp);
    servidor.agregarRuta("GET", "/api/sesion", manejarSesionHttp);
    servidor.agregarRuta("POST", "/api/login", manejarLoginHttp);
    servidor.agregarRuta("POST", "/api/logout", manejarLogoutHttp);
    servidor.agregarRuta("POST", "/api/fs/listar", manejarListarDirectorioHttp);
    servidor.agregarRuta("POST", "/api/fs/archivo", manejarLeerArchivoHttp);
    servidor.agregarRuta("POST", "/api/journaling", manejarJournalHttp);

    std::cout << "======================================================\n";
    std::cout << " C++ DISK ONLINE - API REST para el frontend\n";
    std::cout << "======================================================\n";
    std::cout << "POST http://localhost:" << puerto << "/api/comando\n";
    std::cout << "POST http://localhost:" << puerto << "/api/procesar\n";
    std::cout << "POST http://localhost:" << puerto << "/api/login y /api/logout\n";
    std::cout << "POST http://localhost:" << puerto << "/api/fs/listar y /api/fs/archivo\n";
    std::cout << "GET  http://localhost:" << puerto << "/api/estado y /api/sesion\n\n";

    servidor.escuchar(puerto);  // se queda corriendo para siempre
    return 0;
}