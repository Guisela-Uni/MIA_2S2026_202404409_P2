#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <functional>
#include <string>
#include <vector>


// Lo que llega del frontend
struct PeticionHttp {
    std::string metodo;  // "GET", "POST", "OPTIONS", etc.
    std::string ruta;    // "/api/comando"
    std::string cuerpo;  // el body del POST (JSON), vacio en un GET
    int codigoError = 0;
    std::string mensajeError;
};

// Lo que se devuelve al frontend
struct RespuestaHttp {
    int codigo = 200;                              // 200 OK, 404 No encontrado, etc.
    std::string tipoContenido = "application/json"; // Content-Type
    std::string cuerpo;                             // el JSON de respuesta
};

// Una "ruta" (handler) recibe la peticion y devuelve la respuesta.
using ManejadorHttp = std::function<RespuestaHttp(const PeticionHttp &)>;

class ServidorHttp {
public:
    // Registra que funcion atiende un metodo+ruta especifico.
    // Ej: agregarRuta("POST", "/api/comando", miFuncion);
    void agregarRuta(const std::string &metodo, const std::string &ruta, ManejadorHttp manejador);

    // Arranca el servidor y se queda escuchando para siempre (bloqueante).
    void escuchar(int puerto);

private:
    struct RutaRegistrada {
        std::string metodo;
        std::string ruta;
        ManejadorHttp manejador;
    };

    std::vector<RutaRegistrada> rutas_;

    // Lee una peticion HTTP completa (linea inicial + headers + body)
    // desde un socket de cliente ya conectado.
    PeticionHttp leerPeticion(int socketCliente);

    // Arma el texto HTTP de respuesta (status line + headers + body) y
    // lo envia por el socket.
    void enviarRespuesta(int socketCliente, const RespuestaHttp &respuesta);
};

#endif
