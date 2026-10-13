#include "analizador.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <vector>

// este bloque de includes es para poder usar las estructuras de datos que
// mantienen el "estado" de la simulacion (discos, particiones, usuarios,
#include "structs.h"
#include "estado.h"
#include "mkdisk.h"
#include "rmdisk.h"


//este bloque de includes define las funciones que implementan la logica de cada comando 
#include "fdisk.h"
#include "mount.h"
#include "mkfs.h"
#include "mkusr.h"
#include "rmusr.h"
#include "mkfile.h"
#include "mkdir.h"
#include "cat.h"
#include "login.h"
#include "logout.h"
#include "mkgrp.h"
#include "rmgrp.h"
#include "chgrp.h"
#include "mounted.h"
#include "reports.h"
#include "journal_command.h"
#include "unmount.h"
#include "remove.h"
#include "rename.h"
#include "copy.h"
#include "move.h"
#include "find.h"
#include "chown.h"
#include "chmod.h"
#include "loss.h"
#include "validaciones.h"

namespace {
// Los discos y el sistema EXT2 se persisten en archivos .mia.
// El montaje (MOUNT/MOUNTED) es el estado que deliberadamente permanece en RAM.
// Es UNA sola variable global compartida por todos los comandos, tanto
EstadoGlobal estado_global;
}

// Permite que la api mismo estado en memoria que usan mkdisk/fdisk/mount/etc.
EstadoGlobal &obtener_estado() {
    return estado_global;
}

void lexical_analysis(std::string &string_input, std::vector<std::string> &parametros) {
    //forzar que se cierre
    string_input += " ";

    int clasificacion = 0;

    std::string string_temporal;

    // marca si ya se leyo el nombre del comando (parametros[0])
    bool comando_leido = false;


    for (int i = 0; i < static_cast<int>(string_input.length()); i++) {
        switch (clasificacion) {
            case 0:
                if (!comando_leido && std::isalpha(static_cast<unsigned char>(string_input[i]))) {
                    // Todavia no se ha leido el comando, esta letra lo inicia
                    clasificacion = 1;
                    string_temporal += string_input[i];
                } else if (string_input[i] == '-') {
                    // INICIO DE UN PARAMETRO: -nombre=valor 
                    clasificacion = 2;
                } else if (string_input[i] == '>') {
                    clasificacion = 2;
                } else if (string_input[i] == '#') {
                    // comentario
                    clasificacion = 3;
                }
                // cualquier otro caracter (espacios extra, etc.) se ignora
                break;
            case 1:
            //COMANDO : analiza hasta que encuentra espacio
                if (string_input[i] == ' ') {
                    parametros.push_back(string_temporal);
                    string_temporal.clear();
                    comando_leido = true;
                    clasificacion = 0;
                } else {
                    string_temporal += string_input[i];
                }
                break;
            case 2:
            //PARAMETRO : analiza hasta espacio o comillas (soporta -nombre=valor y -nombre="valor con espacios")
                if (string_input[i] == '"') {
                    // CADENA COMO PARAMETRO
                    clasificacion = 4;
                } else if (string_input[i] == ' ') {
                    // VALORES NORMALES COMO PARAMETRO
                    parametros.push_back(string_temporal);
                    string_temporal.clear();
                    clasificacion = 0;
                } else {
                    string_temporal += string_input[i];
                }
                break;
            case 4:
            //PARAMETRO ENTRE COMILLAS : analiza hasta encontrar comilla de cierre
                if (string_input[i] == '"') {
                    parametros.push_back(string_temporal);
                    string_temporal.clear();
                    clasificacion = 0;
                } else {
                    string_temporal += string_input[i];
                }
                break;
            case 3:
            //COMENTARIO : ignorar todo hasta el final de la linea
                break;
        }
    }

    // por si el input termina dentro de un parametro sin espacio final (defensivo)
    if (!string_temporal.empty()) {
        parametros.push_back(string_temporal);
    }
}


//              LOGICA DE CADA COMANDO (semantica + "ejecucion" simulada)

namespace {

// Llama a la funcion correspondiente (mkdisk, fdisk, mount, etc.) segun
// el nombre del comando. En este punto YA se paso la validacion lexica y sintactica
void despacharComando(const std::string &tipoComandoOriginal, std::vector<std::string> &parametros) {
    std::string tipo_command = tipoComandoOriginal;
    std::transform(tipo_command.begin(), tipo_command.end(), tipo_command.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (tipo_command == "mkdisk") {
        mkdisk(parametros, estado_global);
    } else if (tipo_command == "rmdisk") {
        rmdisk(parametros, estado_global);
    } else if (tipo_command == "fdisk") {
        fdisk(parametros, estado_global);
    } else if (tipo_command == "mount") {
        mount(parametros, estado_global);
    } else if (tipo_command == "unmount") {
        unmount_cmd(parametros, estado_global);
    } else if (tipo_command == "mkfs") {
        mkfs(parametros, estado_global);
    } else if (tipo_command == "login") {
        login(parametros, estado_global);
    } else if (tipo_command == "logout") {
        logout(parametros, estado_global);
    } else if (tipo_command == "mkgrp") {
        mkgrp(parametros, estado_global);
    } else if (tipo_command == "rmgrp") {
        rmgrp(parametros, estado_global);
    } else if (tipo_command == "chgrp") {
        chgrp(parametros, estado_global);
    } else if (tipo_command == "mkusr") {
        mkusr(parametros, estado_global);
    } else if (tipo_command == "rmusr") {
        rmusr(parametros, estado_global);
    } else if (tipo_command == "mkfile") {
        mkfile(parametros, estado_global);
    } else if (tipo_command == "mkdir") {
        mkdir_cmd(parametros, estado_global);
    } else if (tipo_command == "cat") {
        cat(parametros, estado_global);
    } else if (tipo_command == "mounted") {
        mounted(parametros, estado_global);
    } else if (tipo_command == "rep") {
        rep(parametros, estado_global);
    } else if (tipo_command == "journaling") {
        mostrarJournal(parametros, estado_global);
    } else if (tipo_command == "remove") {
        remove_cmd(parametros, estado_global);
    } else if (tipo_command == "rename") {
        rename_cmd(parametros, estado_global);
    } else if (tipo_command == "copy") {
        copy_cmd(parametros, estado_global);
    } else if (tipo_command == "move") {
        move_cmd(parametros, estado_global);
    } else if (tipo_command == "find") {
        find_cmd(parametros, estado_global);
    } else if (tipo_command == "chown") {
        chown_cmd(parametros, estado_global);
    } else if (tipo_command == "chmod") {
        chmod_cmd(parametros, estado_global);
    } else if (tipo_command == "loss") {
        loss_cmd(parametros, estado_global);
    } else {
        // Lexicamente y sintacticamente el comando esta bien escrito,pero no se reconocen en el sistema
        std::cout << "[ERROR] El comando '" << tipoComandoOriginal
                   << "' no es reconocido. Los comandos validos son: MKDISK, RMDISK, FDISK, "
                   << "MOUNT, UNMOUNT, MOUNTED, MKFS, LOGIN, LOGOUT, MKGRP, RMGRP, MKUSR, RMUSR, CHGRP, MKFILE, MKDIR, CAT, REP, JOURNALING, REMOVE, RENAME, COPY, MOVE, FIND, CHOWN, LOSS." << std::endl;
    }
}

} 


std::string procesar_linea_comando(const std::string &lineaComando) {
    std::ostringstream buffer;
    std::streambuf *destinoOriginal = std::cout.rdbuf(buffer.rdbuf());

    // 1ro: tokenizar la linea (separar comando + parametros) 
    std::string copiaLinea = lineaComando;
    std::vector<std::string> parametros;
    lexical_analysis(copiaLinea, parametros);

    if (parametros.empty()) {
        std::cout << "[!] Linea vacia o comentario, no hay nada que analizar." << std::endl;
        std::cout.rdbuf(destinoOriginal);
        return buffer.str();
    }

    //  ANALISIS LEXICO 
    // revisa que ninguna comilla se haya quedado sin cerrar
    ResultadoValidacion comillas = validarComillasCerradas(lineaComando);
    if (!comillas.valido) {
        std::cout << comillas.mensaje << std::endl;
        std::cout.rdbuf(destinoOriginal);
        return buffer.str();
    }

    ResultadoValidacion resultadoLexico = validarLexico(parametros);
    if (!resultadoLexico.valido) {
        std::cout << resultadoLexico.mensaje << std::endl;
        std::cout.rdbuf(destinoOriginal);
        return buffer.str();
    }

    //  ANALISIS SINTACTICO 
    ResultadoValidacion resultadoSintactico = validarSintaxis(parametros);
    if (!resultadoSintactico.valido) {
        std::cout << resultadoSintactico.mensaje << std::endl;
        std::cout.rdbuf(destinoOriginal);
        return buffer.str();
    }

    // 2do LOGICA DEL COMANDO (semantica + "ejecucion" simulada) 
    despacharComando(parametros[0], parametros);

    std::cout.rdbuf(destinoOriginal);
    return buffer.str();
}

// imprime directo en pantalla en vez de devolver el texto.
void run_command(std::string &string_input) {
    std::cout << procesar_linea_comando(string_input);
}

// procesar_linea_comando, con nombre mas claro para quien lea api_server_main.cpp.
std::string ejecutar_comando_texto(const std::string &comando) {
    return procesar_linea_comando(comando);
}


std::string procesar_archivo_comandos(const std::string &contenido) {
    std::ostringstream reporte;
    std::istringstream flujoLineas(contenido);

    std::string lineaOriginal;
    int numeroLinea = 0;
    int comandosProcesados = 0;

    while (std::getline(flujoLineas, lineaOriginal)) {
        numeroLinea++;

        //  quitar espacios/tabs al inicio y al final de la linea 
        size_t inicio = lineaOriginal.find_first_not_of(" \t\r\n");
        size_t fin = lineaOriginal.find_last_not_of(" \t\r\n");
        std::string lineaLimpia = (inicio == std::string::npos) ? "" : lineaOriginal.substr(inicio, fin - inicio + 1);

        // ignorar lineas vacias 
        if (lineaLimpia.empty()) {
            continue;
        }

        // ignorar comentarios (la linea completa empieza con #) 
        if (lineaLimpia[0] == '#') {
            continue;
        }

        // procesar el comando (fases lexico -> sintactico -> logica) 
        comandosProcesados++;
        std::string resultado = procesar_linea_comando(lineaLimpia);

        if (!resultado.empty()) {
            reporte << resultado;

            if (resultado.back() != '\n') {
                reporte << '\n';
            }
        }
    }

    if (comandosProcesados == 0) {
        reporte << "No se encontro ningun comando para procesar.\n";
    } else {
        reporte << "__________________________________________\n";
        reporte << "Total de comandos procesados: " << comandosProcesados << "\n";
    }

    return reporte.str();
}