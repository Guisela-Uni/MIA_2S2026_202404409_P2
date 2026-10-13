#include "http_server.h"

#include "json_util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

namespace {
constexpr std::size_t MAX_ENCABEZADOS = 64 * 1024;
constexpr std::size_t MAX_CUERPO = 256 * 1024 * 1024;
constexpr int TIMEOUT_SOCKET_SEGUNDOS = 15;

std::string minusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return texto;
}

std::string recortar(std::string texto) {
    const auto inicio = texto.find_first_not_of(" \t\r\n");
    if (inicio == std::string::npos) return "";
    const auto final = texto.find_last_not_of(" \t\r\n");
    return texto.substr(inicio, final - inicio + 1);
}

void establecerError(PeticionHttp &peticion, int codigo, const std::string &mensaje) {
    peticion.codigoError = codigo;
    peticion.mensajeError = mensaje;
    peticion.metodo.clear();
    peticion.ruta.clear();
    peticion.cuerpo.clear();
}

bool enviarTodo(int socketCliente, const std::string &textoCompleto) {
    std::size_t enviado = 0;
    while (enviado < textoCompleto.size()) {
        const ssize_t cantidad = send(
            socketCliente,
            textoCompleto.data() + enviado,
            textoCompleto.size() - enviado,
            0);
        if (cantidad <= 0) return false;
        enviado += static_cast<std::size_t>(cantidad);
    }
    return true;
}
} // namespace

void ServidorHttp::agregarRuta(
    const std::string &metodo,
    const std::string &ruta,
    ManejadorHttp manejador
) {
    rutas_.push_back({metodo, ruta, manejador});
}

PeticionHttp ServidorHttp::leerPeticion(int socketCliente) {
    PeticionHttp peticion;
    std::string datosCrudos;
    char bufferLectura[8192];
    std::size_t posicionFinEncabezados = std::string::npos;

    while (posicionFinEncabezados == std::string::npos) {
        const ssize_t leidos = recv(socketCliente, bufferLectura, sizeof(bufferLectura), 0);
        if (leidos <= 0) {
            establecerError(peticion, 400, "No se recibio una peticion HTTP completa.");
            return peticion;
        }

        datosCrudos.append(bufferLectura, static_cast<std::size_t>(leidos));
        posicionFinEncabezados = datosCrudos.find("\r\n\r\n");
        if (posicionFinEncabezados == std::string::npos && datosCrudos.size() > MAX_ENCABEZADOS) {
            establecerError(peticion, 413, "Los encabezados HTTP exceden el limite permitido.");
            return peticion;
        }
    }

    if (posicionFinEncabezados > MAX_ENCABEZADOS) {
        establecerError(peticion, 413, "Los encabezados HTTP exceden el limite permitido.");
        return peticion;
    }

    const std::string encabezados = datosCrudos.substr(0, posicionFinEncabezados);
    const std::string cuerpoInicial = datosCrudos.substr(posicionFinEncabezados + 4);
    std::istringstream lineas(encabezados);
    std::string lineaInicial;
    if (!std::getline(lineas, lineaInicial)) {
        establecerError(peticion, 400, "La solicitud HTTP no contiene una linea inicial.");
        return peticion;
    }

    std::istringstream primeraLinea(lineaInicial);
    std::string versionHttp;
    primeraLinea >> peticion.metodo >> peticion.ruta >> versionHttp;
    if (peticion.metodo.empty() || peticion.ruta.empty() ||
        (versionHttp != "HTTP/1.0" && versionHttp != "HTTP/1.1")) {
        establecerError(peticion, 400, "La linea inicial HTTP no es valida.");
        return peticion;
    }

    std::size_t contentLength = 0;
    bool tieneContentLength = false;
    std::string linea;
    while (std::getline(lineas, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        const std::size_t dosPuntos = linea.find(':');
        if (dosPuntos == std::string::npos) continue;

        const std::string nombre = minusculas(recortar(linea.substr(0, dosPuntos)));
        const std::string valor = recortar(linea.substr(dosPuntos + 1));

        if (nombre == "transfer-encoding" && minusculas(valor) != "identity") {
            establecerError(peticion, 400, "Transfer-Encoding no esta soportado; use Content-Length.");
            return peticion;
        }
        if (nombre != "content-length") continue;

        if (tieneContentLength) {
            establecerError(peticion, 400, "Content-Length no debe aparecer mas de una vez.");
            return peticion;
        }
        if (valor.empty() || !std::all_of(valor.begin(), valor.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            })) {
            establecerError(peticion, 400, "Content-Length debe ser un entero no negativo.");
            return peticion;
        }

        try {
            const unsigned long long longitud = std::stoull(valor);
            if (longitud > MAX_CUERPO) {
                establecerError(peticion, 413, "El cuerpo HTTP excede el limite de 256 MiB.");
                return peticion;
            }
            contentLength = static_cast<std::size_t>(longitud);
        } catch (...) {
            establecerError(peticion, 400, "Content-Length no es valido.");
            return peticion;
        }
        tieneContentLength = true;
    }

    if (!tieneContentLength && !cuerpoInicial.empty()) {
        establecerError(peticion, 400, "La peticion incluye un cuerpo sin Content-Length.");
        return peticion;
    }

    if (cuerpoInicial.size() > contentLength) {
        peticion.cuerpo.assign(cuerpoInicial.data(), contentLength);
    } else {
        peticion.cuerpo = cuerpoInicial;
    }

    while (peticion.cuerpo.size() < contentLength) {
        const ssize_t leidos = recv(socketCliente, bufferLectura, sizeof(bufferLectura), 0);
        if (leidos <= 0) {
            establecerError(peticion, 400, "El cuerpo HTTP llego incompleto.");
            return peticion;
        }

        const std::size_t restantes = contentLength - peticion.cuerpo.size();
        const std::size_t cantidad = std::min<std::size_t>(
            restantes, static_cast<std::size_t>(leidos));
        peticion.cuerpo.append(bufferLectura, cantidad);
    }

    return peticion;
}

void ServidorHttp::enviarRespuesta(int socketCliente, const RespuestaHttp &respuesta) {
    const char *textoEstado = "Error";
    switch (respuesta.codigo) {
        case 200: textoEstado = "OK"; break;
        case 204: textoEstado = "No Content"; break;
        case 400: textoEstado = "Bad Request"; break;
        case 401: textoEstado = "Unauthorized"; break;
        case 403: textoEstado = "Forbidden"; break;
        case 404: textoEstado = "Not Found"; break;
        case 413: textoEstado = "Payload Too Large"; break;
        case 500: textoEstado = "Internal Server Error"; break;
        default: break;
    }

    std::ostringstream http;
    http << "HTTP/1.1 " << respuesta.codigo << ' ' << textoEstado << "\r\n";
    http << "Content-Type: " << respuesta.tipoContenido << "\r\n";
    http << "Content-Length: " << respuesta.cuerpo.size() << "\r\n";
    http << "Access-Control-Allow-Origin: *\r\n";
    http << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    http << "Access-Control-Allow-Headers: Content-Type\r\n";
    http << "Connection: close\r\n\r\n";
    http << respuesta.cuerpo;
    enviarTodo(socketCliente, http.str());
}

void ServidorHttp::escuchar(int puerto) {
    const int socketServidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketServidor < 0) {
        std::cerr << "[ERROR] No se pudo crear el socket.\n";
        return;
    }

    int reusar = 1;
    setsockopt(socketServidor, SOL_SOCKET, SO_REUSEADDR, &reusar, sizeof(reusar));

    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(static_cast<uint16_t>(puerto));

    if (bind(socketServidor, reinterpret_cast<sockaddr *>(&direccion), sizeof(direccion)) < 0) {
        std::cerr << "[ERROR] No se pudo enlazar al puerto " << puerto << ".\n";
        close(socketServidor);
        return;
    }
    if (listen(socketServidor, 10) < 0) {
        std::cerr << "[ERROR] No se pudo escuchar en el puerto " << puerto << ".\n";
        close(socketServidor);
        return;
    }

    std::cout << "[API] Servidor escuchando en http://localhost:" << puerto << std::endl;
    while (true) {
        const int socketCliente = accept(socketServidor, nullptr, nullptr);
        if (socketCliente < 0) continue;

        timeval timeout{};
        timeout.tv_sec = TIMEOUT_SOCKET_SEGUNDOS;
        setsockopt(socketCliente, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        PeticionHttp peticion = leerPeticion(socketCliente);
        if (peticion.codigoError != 0) {
            RespuestaHttp error;
            error.codigo = peticion.codigoError;
            error.cuerpo = "{\"error\":\"" + escapar_json(peticion.mensajeError) + "\"}";
            enviarRespuesta(socketCliente, error);
            close(socketCliente);
            continue;
        }

        if (peticion.metodo == "OPTIONS") {
            enviarRespuesta(socketCliente, RespuestaHttp{204, "text/plain", ""});
            close(socketCliente);
            continue;
        }

        const std::size_t inicioQuery = peticion.ruta.find('?');
        const std::string rutaEnrutamiento = peticion.ruta.substr(0, inicioQuery);
        bool encontrada = false;
        for (const auto &ruta : rutas_) {
            if (ruta.metodo != peticion.metodo || ruta.ruta != rutaEnrutamiento) continue;
            try {
                enviarRespuesta(socketCliente, ruta.manejador(peticion));
            } catch (const std::exception &error) {
                std::cerr << "[ERROR] Excepcion al procesar la peticion: " << error.what() << '\n';
                enviarRespuesta(socketCliente, RespuestaHttp{
                    500, "application/json", R"({"error":"Error interno al procesar la peticion."})"
                });
            } catch (...) {
                std::cerr << "[ERROR] Excepcion desconocida al procesar la peticion.\n";
                enviarRespuesta(socketCliente, RespuestaHttp{
                    500, "application/json", R"({"error":"Error interno al procesar la peticion."})"
                });
            }
            encontrada = true;
            break;
        }

        if (!encontrada) {
            enviarRespuesta(socketCliente, RespuestaHttp{
                404, "application/json", R"({"error":"ruta no encontrada"})"
            });
        }
        close(socketCliente);
    }
}
