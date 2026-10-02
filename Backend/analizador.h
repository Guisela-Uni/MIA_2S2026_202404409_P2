#ifndef CLASE1_ANALIZADOR_H
#define CLASE1_ANALIZADOR_H

#include <string>
#include <vector>

#include "estado.h"


// Separa "mkdisk -size=5 -path=/x.mia" en ["mkdisk", "size=5", "path=/x.mia"]
void lexical_analysis(std::string &string_input, std::vector<std::string> &parametros);

// Ejecuta un comando e IMPRIME el resultado directo en consola (std::cout, lo usa el modo interactivo de main.cpp.
void run_command(std::string &string_input);

// Ejecuta UNA linea de comando pasando por las 3 fases (lexico -> sintactico-> logica del comando) y DEVUELVE el resultado como texto
std::string procesar_linea_comando(const std::string &lineaComando);

// Igual que procesar_linea_comando, pero pensada para la API de "un solocomando" (/api/comando).
std::string ejecutar_comando_texto(const std::string &comando);

// Recibe el CONTENIDO COMPLETO y devuelve un REPORTE en texto con el resultado de cada linea
std::string procesar_archivo_comandos(const std::string &contenido);

// Da acceso al estado global simulado (discos, particiones, usuarios
EstadoGlobal &obtener_estado();

#endif
