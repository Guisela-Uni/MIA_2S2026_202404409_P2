#include "mkfs.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return s;
}

bool split(const std::string& t, std::string& k, std::string& v) {
    auto p = t.find('=');
    if (p == std::string::npos) {
        return false;
    }
    
    k = lower(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

void initInode(inodo& i) {
    std::memset(&i, 0, sizeof(i));
    for (int& b : i.i_block) {
        b = -1;
    }
}

void initFolder(bcarpetas& b) {
    std::memset(&b, 0, sizeof(b));
    for (auto& c : b.b_content) {
        c.b_inodo = -1;
    }
}

} // namespace

void mkfs(std::vector<std::string>& parametros, EstadoGlobal& estado) {
    std::string id, type = "full";
    
    for (size_t i = 1; i < parametros.size(); ++i) {
        std::string k, v;
        
        if (!split(parametros[i], k, v)) {
            std::cout << "[ERROR] MKFS requiere clave=valor.\n";
            return;
        }
        
        if (k == "id") {
            id = v;
        } else if (k == "type") {
            type = lower(v);
        } else {
            std::cout << "[ERROR] Parametro no reconocido en MKFS: -" << k << "\n";
            return;
        }
    }
    
    if (id.empty()) {
        std::cout << "[ERROR] MKFS requiere -id.\n";
        return;
    }
    
    if (type != "full") {
        std::cout << "[ERROR] MKFS solo soporta -type=full.\n";
        return;
    }
    
    disco* dm = nullptr;
    montada* pm = nullptr;
    
    if (!buscarMontada(estado, id, dm, pm)) {
        std::cout << "[ERROR] No existe una particion montada con ID " << id << ".\n";
        return;
    }
    
    int start = 0, size = 0;
    
    if (pm->posMBR >= 0) {
        SimDisk d;
        if (!cargarDisco(dm->ruta, d)) {
            std::cout << "[ERROR] No se pudo leer el disco.\n";
            return;
        }
        start = d.particiones[pm->posMBR].part_start;
        size = d.particiones[pm->posMBR].part_s;
    } else {
        EBR e{};
        if (!leerEBR(dm->ruta, pm->posEBR, e)) {
            std::cout << "[ERROR] No se pudo leer EBR.\n";
            return;
        }
        start = e.part_start;
        size = e.part_s;
    }
    
    const int sb = sizeof(sbloque);
    const int ino = sizeof(inodo);
    const int bc = sizeof(bcarpetas);
    
    int n = (size - sb) / (1 + 3 + ino + 3 * bc);
    
    if (n < 2) {
        std::cout << "[ERROR] La particion es demasiado pequena para EXT2.\n";
        return;
    }
    
    int blocks = 3 * n;
    
    sbloque super{};
    super.s_filesystem_type = 2;
    super.s_inodes_count = n;
    super.s_blocks_count = blocks;
    super.s_free_inodes_count = n - 2;
    super.s_free_blocks_count = blocks - 2;
    super.s_mtime = std::time(nullptr);
    super.s_umtime = 0;
    super.s_mnt_count = 1;
    super.s_magic = EXT2_MAGIC;
    super.s_inode_s = sizeof(inodo);
    super.s_block_s = sizeof(bcarpetas);
    super.s_first_ino = 2;
    super.s_first_blo = 2;
    super.s_bm_inode_start = start + sb;
    super.s_bm_block_start = super.s_bm_inode_start + n;
    super.s_inode_start = super.s_bm_block_start + blocks;
    super.s_block_start = super.s_inode_start + n * ino;
    
    std::fstream f(dm->ruta, std::ios::in | std::ios::out | std::ios::binary);
    
    if (!f) {
        std::cout << "[ERROR] No se pudo abrir el .mia para MKFS.\n";
        return;
    }
    
    auto writeAt = [&](std::streamoff off, const void* data, std::streamsize nbytes) {
        f.seekp(off);
        f.write(reinterpret_cast<const char*>(data), nbytes);
        return bool(f);
    };
    
    if (!writeAt(start, &super, sizeof(super))) {
        std::cout << "[ERROR] No se pudo escribir el superbloque.\n";
        return;
    }
    
    std::vector<char> bmi(n, 0), bmb(blocks, 0);
    bmi[0] = bmi[1] = 1;
    bmb[0] = bmb[1] = 1;
    
    if (!writeAt(super.s_bm_inode_start, bmi.data(), bmi.size()) || 
        !writeAt(super.s_bm_block_start, bmb.data(), bmb.size())) {
        std::cout << "[ERROR] No se pudieron escribir los bitmaps.\n";
        return;
    }
    
    inodo root{};
    initInode(root);
    root.i_uid = 1;
    root.i_gid = 1;
    root.i_s = 0;
    root.i_atime = root.i_ctime = root.i_mtime = std::time(nullptr);
    root.i_block[0] = 0;
    root.i_type = 0;
    root.i_perm = 777;
    
    inodo users{};
    initInode(users);
    users.i_uid = 1;
    users.i_gid = 1;
    users.i_s = 32;
    users.i_atime = users.i_ctime = users.i_mtime = std::time(nullptr);
    users.i_block[0] = 1;
    users.i_type = 1;
    users.i_perm = 664;
    
    if (!writeAt(super.s_inode_start, &root, sizeof(root)) || 
        !writeAt(super.s_inode_start + sizeof(inodo), &users, sizeof(users))) {
        std::cout << "[ERROR] No se pudieron escribir los inodos.\n";
        return;
    }
    
    bcarpetas folder{};
    initFolder(folder);
    std::strncpy(folder.b_content[0].b_name, ".", 11);
    folder.b_content[0].b_inodo = 0;
    std::strncpy(folder.b_content[1].b_name, "..", 11);
    folder.b_content[1].b_inodo = 0;
    std::strncpy(folder.b_content[2].b_name, "users.txt", 11);
    folder.b_content[2].b_inodo = 1;
    
    barchivos usersBlock{};
    std::memset(&usersBlock, 0, sizeof(usersBlock));
    const std::string usersText = "1, G, root\n1, U, root, root, 123\n";
    std::memcpy(usersBlock.b_content, usersText.data(), usersText.size());
    
    if (!writeAt(super.s_block_start, &folder, sizeof(folder)) || 
        !writeAt(super.s_block_start + sizeof(bcarpetas), &users