#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <string>

std::string extraer_campo_json(const std::string &json, const std::string &campo);

// Convierte un texto normal en un texto seguro para meter DENTRO de comillas de un JSON (escapa \, ", saltos de linea, tabs, etc.)
std::string escapar_json(const std::string &texto);

#endif
