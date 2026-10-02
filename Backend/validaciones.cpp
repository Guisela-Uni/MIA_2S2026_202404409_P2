#include "validaciones.h"

#include <cctype>

namespace {

// true si el texto contiene UNICAMENTE letras (a-z, A-Z)
bool soloLetras(const std::string &texto) {
    if (texto.empty()) return false;
    for (unsigned char c : texto) {
        if (!std::isalpha(c) && c != '_') return false;
    }
    return true;
}

bool nombreParametroValido(const std::string &texto) {
    if (texto.empty()) return false;
    for (unsigned char c : texto) {
        if (!std::isalnum(c) && c != '_') return false;
    }
    return true;
}

// Devuelve el primer caracter de "texto" que NO es una letra, como texto
std::string primerCaracterInvalido(const std::string &texto) {
    for (char c : texto) {
        if (!std::isalpha(static_cast<unsigned char>(c))) {
            return std::string(1, c);
        }
    }
    return "";
}

}

ResultadoValidacion validarComillasCerradas(const std::string &lineaOriginal) {
    int cantidadComillas = 0;
    for (char c : lineaOriginal) {
        if (c == '"') cantidadComillas++;
    }

    if (cantidadComillas % 2 != 0) {
        return {false, "Error sintactico: hay una comilla (\") sin cerrar en la linea."};
    }
    return {true, ""};
}

ResultadoValidacion validarLexico(const std::vector<std::string> &parametros) {
    if (parametros.empty()) {
        return {true, ""};  // no es un error lexico
    }

    // 1 Validar el nombre del comando (parametros[0]) 
    const std::string &comando = parametros[0];
    if (!soloLetras(comando)) {
        std::string caracter = primerCaracterInvalido(comando);
        return {false,
                "Error lexico: el comando '" + comando +
                    "' contiene un caracter no valido ('" + caracter +
                    "'). El nombre de un comando solo puede tener letras."};
    }

    // 2 Validar el NOMBRE de cada parametro (antes del "=") 
    for (size_t i = 1; i < parametros.size(); ++i) {
        const std::string &token = parametros[i];

        // el nombre del parametro es todo lo que hay antes del primer "="
        size_t posIgual = token.find('=');
        std::string nombreParametro = (posIgual == std::string::npos) ? token : token.substr(0, posIgual);

        if (!nombreParametroValido(nombreParametro)) {
            std::string caracter = primerCaracterInvalido(nombreParametro);
            return {false,
                    "Error lexico: el parametro '-" + nombreParametro +
                        "' contiene un caracter no valido ('" + caracter +
                        "'). El nombre de un parametro solo puede contener letras, numeros y guion bajo."};
        }
    }

    return {true, ""};
}

ResultadoValidacion validarSintaxis(const std::vector<std::string> &parametros) {
    if (parametros.empty()) {
        return {true, ""};
    }

    for (size_t i = 1; i < parametros.size(); ++i) {
        const std::string &token = parametros[i];

        size_t posIgual = token.find('=');
        if (posIgual == std::string::npos) {
            // parametro tipo bandera (sin "="), sintacticamente valido
            continue;
        }

        std::string nombre = token.substr(0, posIgual);
        std::string valor = token.substr(posIgual + 1);

        if (nombre.empty()) {
            return {false, "Error sintactico: hay un '=' sin un nombre de parametro antes."};
        }

        if (valor.empty()) {
            return {false,
                    "Error sintactico: el parametro '-" + nombre +
                        "' no tiene un valor asignado despues del '='."};
        }
    }

    return {true, ""};
}