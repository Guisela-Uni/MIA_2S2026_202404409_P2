#include "json_util.h"

#include <cstdio>
#include <iomanip>
#include <sstream>

std::string escapar_json(const std::string &texto) {
    std::ostringstream resultado;
    resultado << std::hex << std::uppercase;

    for (unsigned char caracter : texto) {
        switch (caracter) {
            case '"': resultado << "\\\""; break;
            case '\\': resultado << "\\\\"; break;
            case '\b': resultado << "\\b"; break;
            case '\f': resultado << "\\f"; break;
            case '\n': resultado << "\\n"; break;
            case '\r': resultado << "\\r"; break;
            case '\t': resultado << "\\t"; break;
            default:
                if (caracter < 0x20) {
                    resultado << "\\u00" << std::setw(2) << std::setfill('0')
                              << static_cast<int>(caracter);
                } else {
                    resultado << static_cast<char>(caracter);
                }
        }
    }

    return resultado.str();
}

std::string extraer_campo_json(const std::string &json, const std::string &campo) {
    const std::string buscado = "\"" + campo + "\"";
    const std::size_t posicionCampo = json.find(buscado);
    if (posicionCampo == std::string::npos) return "";

    const std::size_t posicionDosPuntos = json.find(':', posicionCampo + buscado.size());
    if (posicionDosPuntos == std::string::npos) return "";

    const std::size_t apertura = json.find('"', posicionDosPuntos + 1);
    if (apertura == std::string::npos) return "";

    std::string valor;
    for (std::size_t i = apertura + 1; i < json.size();) {
        const char caracter = json[i++];
        if (caracter == '"') return valor;
        if (caracter != '\\') {
            valor += caracter;
            continue;
        }
        if (i >= json.size()) return "";

        const char escape = json[i++];
        switch (escape) {
            case '"': valor += '"'; break;
            case '\\': valor += '\\'; break;
            case '/': valor += '/'; break;
            case 'b': valor += '\b'; break;
            case 'f': valor += '\f'; break;
            case 'n': valor += '\n'; break;
            case 'r': valor += '\r'; break;
            case 't': valor += '\t'; break;
            case 'u': {
                // Este backend recibe textos UTF-8; el JSON de la interfaz normalmente
                // envía esos caracteres directamente. Admitimos escapes ASCII \u00XX.
                if (i + 4 > json.size()) return "";
                unsigned int codigo = 0;
                for (int digito = 0; digito < 4; ++digito) {
                    const char hex = json[i++];
                    codigo <<= 4;
                    if (hex >= '0' && hex <= '9') codigo += static_cast<unsigned int>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') codigo += static_cast<unsigned int>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') codigo += static_cast<unsigned int>(hex - 'A' + 10);
                    else return "";
                }
                if (codigo <= 0x7F) {
                    valor += static_cast<char>(codigo);
                } else if (codigo <= 0x7FF) {
                    valor += static_cast<char>(0xC0 | (codigo >> 6));
                    valor += static_cast<char>(0x80 | (codigo & 0x3F));
                } else {
                    valor += static_cast<char>(0xE0 | (codigo >> 12));
                    valor += static_cast<char>(0x80 | ((codigo >> 6) & 0x3F));
                    valor += static_cast<char>(0x80 | (codigo & 0x3F));
                }
                break;
            }
            default: return "";
        }
    }
    return "";
}