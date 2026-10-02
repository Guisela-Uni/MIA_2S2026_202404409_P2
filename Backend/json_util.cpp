#include "json_util.h"

std::string escapar_json(const std::string &texto) {
    std::string resultado;
    resultado.reserve(texto.size());

    for (char c : texto) {
        switch (c) {
            case '"':  resultado += "\\\""; break;
            case '\\': resultado += "\\\\"; break;
            case '\n': resultado += "\\n";  break;
            case '\r': resultado += "\\r";  break;
            case '\t': resultado += "\\t";  break;
            default:   resultado += c;      break;
        }
    }
    return resultado;
}

std::string extraer_campo_json(const std::string &json, const std::string &campo) {
    // busca el texto literal "campo"  (ej:  "comando" )
    std::string buscado = "\"" + campo + "\"";
    size_t pos_campo = json.find(buscado);
    if (pos_campo == std::string::npos) {
        return "";
    }

    // A partir de ahi buscamos los dos puntos ":"
    size_t pos_dos_puntos = json.find(':', pos_campo + buscado.size());
    if (pos_dos_puntos == std::string::npos) {
        return "";
    }

    // Luego la primera comilla que abre el valor
    size_t pos_comilla_abre = json.find('"', pos_dos_puntos);
    if (pos_comilla_abre == std::string::npos) {
        return "";
    }

    std::string valor;
    size_t i = pos_comilla_abre + 1;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            // caracter escapado: tomamos el siguiente tal cual
            char siguiente = json[i + 1];
            if (siguiente == 'n') valor += '\n';
            else if (siguiente == 't') valor += '\t';
            else valor += siguiente;  // \" \\ etc.
            i += 2;
        } else {
            valor += json[i];
            i += 1;
        }
    }

    return valor;
}
