#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>

void ServidorHttp::agregarRuta(const std::string &metodo, const std::string &ruta, ManejadorHttp manejador) {
    rutas_.push_back({metodo, ruta, manejador});
}


// Lee todo lo que manda el navegador por el socket: la linea inicial ("POST /api/comando HTTP/1.1"), los headers, y el body si lo hay.
PeticionHttp ServidorHttp::leerPeticion(int socketCliente) {
    std::string datosCrudos;
    char bufferLectura[4096];

    // Se lee en pedazos hasta encontrar la linea vacia que separa
    // los headers del body ("\r\n\r\n"). Eso siempre existe en HTTP.
    size_t posFinHeaders = std::string::npos;
    while (posFinHeaders == std::string::npos) {
        ssize_t leidos = recv(socketCliente, bufferLectura, sizeof(bufferLectura), 0);
        if (leidos <= 0) {
            // el cliente cerro la conexion o hubo un error de red
            return PeticionHttp{};
        }
        datosCrudos.append(bufferLectura, leidos);
        posFinHeaders = datosCrudos.find("\r\n\r\n");
    }

    std::string encabezados = datosCrudos.substr(0, posFinHeaders);
    std::string bodyLeidoHastaAhora = datosCrudos.substr(posFinHeaders + 4);

    //  Parsear la primera linea: "METODO /ruta HTTP/1.1"
    std::istringstream streamEncabezados(encabezados);
    std::string primeraLinea;
    std::getline(streamEncabezados, primeraLinea);

    PeticionHttp peticion;
    std::istringstream streamPrimeraLinea(primeraLinea);
    std::string versionHttp;
    streamPrimeraLinea >> peticion.metodo >> peticion.ruta >> versionHttp;

    //  Buscar el header Content-Length para saber cuanto body falta
    int contentLength = 0;
    std::string linea;
    while (std::getline(streamEncabezados, linea)) {
        // los headers de Linux/HTTP terminan en \r\n, getline deja el \r
        if (!linea.empty() && linea.back() == '\r') {
            linea.pop_back();
        }
        // buscamos "Content-Length:" sin importar mayus/minus
        std::string nombreHeader = linea.substr(0, linea.find(':'));
        for (auto &c : nombreHeader) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (nombreHeader == "content-length") {
            size_t posDosPuntos = linea.find(':');
            if (posDosPuntos != std::string::npos) {
                contentLength = std::stoi(linea.substr(posDosPuntos + 1));
            }
        }
    }

    //  Completar el body si todavia faltan bytes por leer
    peticion.cuerpo = bodyLeidoHastaAhora;
    while (static_cast<int>(peticion.cuerpo.size()) < contentLength) {
        ssize_t leidos = recv(socketCliente, bufferLectura, sizeof(bufferLectura), 0);
        if (leidos <= 0) break;
        peticion.cuerpo.append(bufferLectura, leidos);
    }

    return peticion;
}

// Convierte una RespuestaHttp en texto HTTP real y lo manda al navegador.
void ServidorHttp::enviarRespuesta(int socketCliente, const RespuestaHttp &respuesta) {
    std::string textoEstado = (respuesta.codigo == 200) ? "OK"
                             : (respuesta.codigo == 204) ? "No Content"
                             : (respuesta.codigo == 404) ? "Not Found"
                             : (respuesta.codigo == 400) ? "Bad Request"
                             : "Error";

    std::ostringstream http;
    http << "HTTP/1.1 " << respuesta.codigo << " " << textoEstado << "\r\n";
    http << "Content-Type: " << respuesta.tipoContenido << "\r\n";
    http << "Content-Length: " << respuesta.cuerpo.size() << "\r\n";

    // CORS: permite que el frontend (que corre en otro puerto, ej. 5173)
    // pueda llamar a esta API sin que el navegador lo bloquee.
    http << "Access-Control-Allow-Origin: *\r\n";
    http << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    http << "Access-Control-Allow-Headers: Content-Type\r\n";

    http << "Connection: close\r\n";
    http << "\r\n";
    http << respuesta.cuerpo;

    std::string textoCompleto = http.str();
    send(socketCliente, textoCompleto.c_str(), textoCompleto.size(), 0);
}

void ServidorHttp::escuchar(int puerto) {
    // 1. Crear el socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    int socketServidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketServidor < 0) {
        std::cerr << "[ERROR] No se pudo crear el socket" << std::endl;
        return;
    }

    // Permite reiniciar el servidor rapido sin esperar a que el SO
    // libere el puerto (util mientras se prueba/desarrolla)
    int reusar = 1;
    setsockopt(socketServidor, SOL_SOCKET, SO_REUSEADDR, &reusar, sizeof(reusar));

    // 2. Asociar el socket a un puerto en todas las interfaces (0.0.0.0)
    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(static_cast<uint16_t>(puerto));

    if (bind(socketServidor, reinterpret_cast<sockaddr *>(&direccion), sizeof(direccion)) < 0) {
        std::cerr << "[ERROR] No se pudo enlazar al puerto " << puerto << std::endl;
        close(socketServidor);
        return;
    }

    // 3. Ponerse a escuchar conexiones entrantes (cola de hasta 10)
    if (listen(socketServidor, 10) < 0) {
        std::cerr << "[ERROR] No se pudo escuchar en el puerto " << puerto << std::endl;
        close(socketServidor);
        return;
    }

    std::cout << "[API] Servidor escuchando en http://localhost:" << puerto << std::endl;

    // 4. Bucle principal: aceptar clientes uno por uno (bloqueante,
    // simple de seguir). Cada peticion se atiende y se cierra antes de aceptar la siguiente.
    while (true) {
        int socketCliente = accept(socketServidor, nullptr, nullptr);
        if (socketCliente < 0) {
            continue;
        }

        PeticionHttp peticion = leerPeticion(socketCliente);

        // Preflight de CORS: el navegador manda un OPTIONS antes del
        // POST real para preguntar "me dejas?". Respondemos que si.
        if (peticion.metodo == "OPTIONS") {
            enviarRespuesta(socketCliente, RespuestaHttp{204, "text/plain", ""});
            close(socketCliente);
            continue;
        }

        // Buscar una ruta registrada que haga match con metodo + ruta
        bool encontrada = false;
        for (const auto &r : rutas_) {
            if (r.metodo == peticion.metodo && r.ruta == peticion.ruta) {
                RespuestaHttp respuesta = r.manejador(peticion);
                enviarRespuesta(socketCliente, respuesta);
                encontrada = true;
                break;
            }
        }

        if (!encontrada) {
            RespuestaHttp noEncontrada;
            noEncontrada.codigo = 404;
            noEncontrada.cuerpo = "{\"error\":\"ruta no encontrada\"}";
            enviarRespuesta(socketCliente, noEncontrada);
        }

        close(socketCliente);
    }

    close(socketServidor);
}
