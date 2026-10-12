#ifndef STRUCTS_H
#define STRUCTS_H

#include <ctime>
#include <string>
#include <vector>
#include <regex>

// Estructuras binarias del Proyecto 1. No se deben agregar std::string/vector
// a estas estructuras porque se escriben directamente en el .mia.
struct particion {
    char part_status;          // 0 = no montada, 1 = montada (solo RAM)
    char part_type;            // p = primaria, e = extendida
    char part_fit;             // f = FF, b = BF, w = WF
    int  part_start;
    int  part_s;
    char part_name[16];
    int part_correlativ;       // -1 hasta que se monte
    char part_id[4];            // ID del mount, solo RAM
};

struct MBR {
    int mbr_tamano;
    std::time_t mbr_fecha_creacion;
    int mbr_dsk_signature;
    char dsk_fit;
    particion mbr_partition[4];
};

struct EBR {
    char part_status;           // solo RAM
    char part_fit;
    int part_start;             // inicio de datos de la logica
    int part_s;
    int part_next;              // offset del siguiente EBR, -1 si no existe
    char part_name[16];
};

struct position {
    int inicio;
    int fin;
    char tipo;
    std::string nombre;
    int size;
    bool operator<(const position& a) const { return inicio < a.inicio; }
};

struct libre {
    int inicio;
    int size;
    bool operator<(const libre& a) const { return size < a.size; }
};

struct libreL {
    int inicioEBR;
    int finLogica;
    int size;
    bool header;
    bool operator<(const libreL& a) const { return size < a.size; }
};

struct montada {
    std::string id;
    int posEBR = -1;
    int posMBR = -1;
    std::string nombre;
    std::string ruta;
    int size = 0;
};

struct disco {
    std::string ruta;
    std::string nombre;
    char letra = 'A';
    int contador = 1;
    std::vector<montada> particiones;
};

struct sbloque {
    int s_filesystem_type;
    int s_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    int s_free_inodes_count;
    std::time_t s_mtime;
    std::time_t s_umtime;
    int s_mnt_count;
    int s_magic;
    int s_inode_s;
    int s_block_s;
    int s_first_ino;
    int s_first_blo;
    int s_bm_inode_start;
    int s_bm_block_start;
    int s_inode_start;
    int s_block_start;
};

struct inodo {
    int i_uid;
    int i_gid;
    int i_s;
    std::time_t i_atime;
    std::time_t i_ctime;
    std::time_t i_mtime;
    int i_block[15];
    char i_type;                // 0 carpeta, 1 archivo
    int i_perm;                 // octal, por ejemplo 664
};

struct content {
    char b_name[12];
    int b_inodo;
};

struct bcarpetas { content b_content[4]; };
struct barchivos { char b_content[64]; };
struct bapuntadores { int b_pointers[16]; };

struct usuario {
    std::string user;
    std::string pass;
    std::string disco;
    std::string grupo;
    std::string id_user;
    std::string id_grp;
};

constexpr int EndMBR = sizeof(MBR);
constexpr int EXT2_MAGIC = 0xEF53;
extern const std::regex igual;

#endif
