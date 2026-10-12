#ifndef EXT2_UTILS_H
#define EXT2_UTILS_H

#include "estado.h"
#include <string>
#include <vector>
#include <utility>

struct Ext2Context {
    std::string path;
    int start = 0;
    int size = 0;
    sbloque sb{};
};

bool obtenerContexto(EstadoGlobal &estado, const std::string &id, Ext2Context &ctx);
bool sesionActiva(EstadoGlobal &estado, Ext2Context &ctx);
bool leerInodo(const Ext2Context &ctx, int n, inodo &ino);
bool escribirInodo(const Ext2Context &ctx, int n, const inodo &ino);
bool leerBloqueCarpeta(const Ext2Context &ctx, int n, bcarpetas &b);
bool escribirBloqueCarpeta(const Ext2Context &ctx, int n, const bcarpetas &b);
bool leerBloqueArchivo(const Ext2Context &ctx, int n, barchivos &b);
bool escribirBloqueArchivo(const Ext2Context &ctx, int n, const barchivos &b);
bool leerBloquePunteros(const Ext2Context &ctx, int n, bapuntadores &b);
bool escribirBloquePunteros(const Ext2Context &ctx, int n, const bapuntadores &b);
bool guardarSuper(const Ext2Context &ctx);

int asignarInodo(Ext2Context &ctx);
int asignarBloque(Ext2Context &ctx);
bool liberarInodo(Ext2Context &ctx, int n);
bool liberarBloque(Ext2Context &ctx, int n);

std::string normalizarRuta(const std::string &path);
std::vector<std::string> partesRuta(const std::string &path);
bool resolverRuta(const Ext2Context &ctx, const std::string &path, int &ino, int *padre = nullptr, std::string *nombre = nullptr);
// Comprueba la relacion real de parentesco usando las entradas ".." del filesystem.
bool esDescendienteDirectorio(const Ext2Context &ctx, int posibleAncestro, int directorioDestino);
bool buscarEntrada(const Ext2Context &ctx, int dirIno, const std::string &nombre, int &ino, int *bloque = nullptr, int *slot = nullptr);
bool agregarEntrada(Ext2Context &ctx, int dirIno, const std::string &nombre, int childIno);
bool eliminarEntrada(Ext2Context &ctx, int dirIno, const std::string &nombre);
bool listarEntradasDirectorio(const Ext2Context &ctx, int dirIno,
                              std::vector<std::pair<std::string, int>> &entradas);
bool cambiarNombreEntrada(const Ext2Context &ctx, int dirIno,
                          const std::string &nombreActual, const std::string &nombreNuevo);
bool actualizarPadreDirectorio(const Ext2Context &ctx, int dirIno, int nuevoPadre);
bool liberarArbolInodo(Ext2Context &ctx, int inoNum);
bool crearDirectorio(Ext2Context &ctx, int padre, const std::string &nombre, int uid, int gid, int &nuevoInodo);
bool crearArchivo(Ext2Context &ctx, int padre, const std::string &nombre, const std::string &contenido, int uid, int gid, int &nuevoInodo);
std::string leerArchivo(const Ext2Context &ctx, const inodo &ino);
bool escribirArchivoCompleto(Ext2Context &ctx, int inoNum, const std::string &contenido);

struct UserRecord { int id=0; char type=' '; std::string group; std::string user; std::string pass; };
std::vector<UserRecord> leerUsers(const Ext2Context &ctx);
bool escribirUsers(Ext2Context &ctx, const std::vector<UserRecord> &records);
int gidActivo(const std::vector<UserRecord> &records, const std::string &grupo);
int uidActivo(const std::vector<UserRecord> &records, const std::string &user, const std::string &pass);
std::string grupoUsuario(const std::vector<UserRecord> &records, const std::string &user);

bool puedeLeer(const inodo &ino, int uid, int gid);
bool puedeEscribir(const inodo &ino, int uid, int gid);
bool puedeEjecutar(const inodo &ino, int uid, int gid);

#endif
