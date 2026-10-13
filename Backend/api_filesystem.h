#ifndef API_FILESYSTEM_H
#define API_FILESYSTEM_H

#include "http_server.h"

RespuestaHttp manejarLoginHttp(const PeticionHttp &peticion);
RespuestaHttp manejarLogoutHttp(const PeticionHttp &peticion);
RespuestaHttp manejarSesionHttp(const PeticionHttp &peticion);
RespuestaHttp manejarListarDirectorioHttp(const PeticionHttp &peticion);
RespuestaHttp manejarLeerArchivoHttp(const PeticionHttp &peticion);
RespuestaHttp manejarJournalHttp(const PeticionHttp &peticion);
RespuestaHttp manejarHealthHttp(const PeticionHttp &peticion);

#endif
