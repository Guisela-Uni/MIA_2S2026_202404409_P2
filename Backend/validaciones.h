#ifndef VALIDACIONES_H
#define VALIDACIONES_H

#include <string>
#include <vector>

struct ResultadoValidacion {
    bool valido = true;
    std::string mensaje;  // solo tiene contenido si valido == false
};


ResultadoValidacion validarComillasCerradas(const std::string &lineaOriginal);
ResultadoValidacion validarLexico(const std::vector<std::string> &parametros);
ResultadoValidacion validarSintaxis(const std::vector<std::string> &parametros);

#endif
