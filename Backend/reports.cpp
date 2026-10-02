#include "reports.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <sys/wait.h>
#include <cstdlib>
#include <functional>

namespace {

std::string low(std::string s) {
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
    k = low(t.substr(0, p));
    v = t.substr(p + 1);
    return true;
}

std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == 0) continue;
        if (c == '&') r += "&amp;";
        else if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;";
        else if (c == '\n' || c == '\r') r += ' ';
        else r += c;
    }
    return r;
}

std::string q(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

struct DiskCtx {
    std::string path;
    int partitionStart = 0;
    int partitionSize = 0;
};

struct FS {
    std::string path;
    int start = 0;
    int size = 0;
    sbloque sb{};
};

bool diskFromId(EstadoGlobal& estado, const std::string& id, DiskCtx& dctx) {
    disco* d = nullptr;
    montada* p = nullptr;
    if (!buscarMontada(estado, id, d, p)) return false;
    
    dctx.path = d->ruta;
    if (p->posMBR >= 0) {
        SimDisk sd;
        if (!cargarDisco(dctx.path, sd)) return false;
        if (p->posMBR < 0 || p->posMBR >= 4) return false;
        dctx.partitionStart = sd.particiones[p->posMBR].part_start;
        dctx.partitionSize = sd.particiones[p->posMBR].part_s;
    } else {
        EBR e{};
        if (!leerEBR(dctx.path, p->posEBR, e)) return false;
        dctx.partitionStart = e.part_start;
        dctx.partitionSize = e.part_s;
    }
    return true;
}

bool fsFrom(EstadoGlobal& estado, const std::string& id, FS& fs) {
    DiskCtx dctx;
    if (!diskFromId(estado, id, dctx)) return false;
    
    fs.path = dctx.path;
    fs.start = dctx.partitionStart;
    fs.size = dctx.partitionSize;
    
    std::ifstream f(fs.path, std::ios::binary);
    if (!f) return false;
    
    f.seekg(fs.start);
    f.read(reinterpret_cast<char*>(&fs.sb), sizeof(fs.sb));
    return bool(f) && fs.sb.s_magic == EXT2_MAGIC;
}

template<class T>
bool rd(std::ifstream& f, std::streamoff off, T& x) {
    f.seekg(off);
    f.read(reinterpret_cast<char*>(&x), sizeof(T));
    return bool(f);
}

bool inode(const FS& fs, int n, inodo& i) {
    if (n < 0 || n >= fs.sb.s_inodes_count) return false;
    std::ifstream f(fs.path, std::ios::binary);
    return rd(f, fs.sb.s_inode_start + std::streamoff(n) * sizeof(inodo), i);
}

bool blockFolder(const FS& fs, int n, bcarpetas& b) {
    std::ifstream f(fs.path, std::ios::binary);
    return rd(f, fs.sb.s_block_start + std::streamoff(n) * sizeof(bcarpetas), b);
}

bool blockFile(const FS& fs, int n, barchivos& b) {
    std::ifstream f(fs.path, std::ios::binary);
    return rd(f, fs.sb.s_block_start + std::streamoff(n) * sizeof(barchivos), b);
}

bool blockPtr(const FS& fs, int n, bapuntadores& b) {
    std::ifstream f(fs.path, std::ios::binary);
    return rd(f, fs.sb.s_block_start + std::streamoff(n) * sizeof(bapuntadores), b);
}

std::string inodePath(const FS& fs, int target) {
    if (target == 0) return "/";
    std::set<int> seen;
    
    std::function<bool(int, std::string&)> rec = [&](int cur, std::string& out) {
        if (cur == target) {
            out = "";
            return true;
        }
        if (seen.count(cur)) return false;
        seen.insert(cur);
        
        inodo i{};
        if (!inode(fs, cur, i) || i.i_type != 0) return false;
        
        for (int bi : i.i_block) {
            if (bi < 0) continue;
            bcarpetas b{};
            if (!blockFolder(fs, bi, b)) continue;
            for (auto& e : b.b_content) {
                if (e.b_inodo < 0 || e.b_inodo == cur) continue;
                std::string child;
                if (rec(e.b_inodo, child)) {
                    std::string name = e.b_name;
                    if (name == "." || name == "..") continue;
                    out = "/" + name + child;
                    return true;
                }
            }
        }
        return false;
    };
    
    std::string out;
    if (rec(0, out)) return out;
    return {};
}

bool findPath(const FS& fs, const std::string& path, int& inoOut) {
    if (path == "/" || path.empty()) {
        inoOut = 0;
        return true;
    }
    
    std::stringstream ss(path);
    std::string part;
    int cur = 0;
    
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        inodo ci{};
        if (!inode(fs, cur, ci) || ci.i_type != 0) return false;
        
        bool ok = false;
        for (int bi : ci.i_block) {
            if (bi < 0) continue;
            bcarpetas b{};
            if (!blockFolder(fs, bi, b)) continue;
            for (auto& e : b.b_content) {
                if (e.b_inodo >= 0 && std::string(e.b_name) == part) {
                    cur = e.b_inodo;
                    ok = true;
                    break;
                }
            }
            if (ok) break;
        }
        if (!ok) return false;
    }
    
    inoOut = cur;
    return true;
}

std::string contentOf(const FS& fs, const inodo& i) {
    std::string s;
    for (int b : i.i_block) {
        if (b < 0) continue;
        barchivos x{};
        if (blockFile(fs, b, x)) {
            s.append(x.b_content, sizeof(x.b_content));
        }
    }
    if (static_cast<int>(s.size()) > i.i_s) {
        s.resize(i.i_s);
    }
    return s;
}

bool dotRender(const std::string& dot, const std::string& out) {
    std::filesystem::path p(out);
    std::string ext = low(p.extension().string());
    
    if (ext == ".dot" || ext == ".gv") {
        std::error_code ec;
        std::filesystem::copy_file(dot, out, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "[ERROR] No se pudo escribir el reporte " << out << ": " << ec.message() << "\n";
        }
        return !ec;
    }
    
    std::string fmt = ext.size() > 1 ? ext.substr(1) : "png";
    if (fmt == "jpg") fmt = "jpg";
    
    std::string cmd = "dot -T" + q(fmt) + " -o " + q(out) + " " + q(dot);
    int rc = std::system(cmd.c_str());
    
    std::error_code ec;
    bool generated = rc == 0 && std::filesystem::exists(out) && std::filesystem::file_size(out, ec) > 0 && !ec;
    if (!generated) {
        std::cerr << "[ERROR] Graphviz no pudo generar " << out << ". Verifique que 'dot' este instalado y disponible en el PATH.\n";
    }
    
    std::filesystem::remove(dot, ec);
    return generated;
}

void writeText(const std::string& p, const std::string& s) {
    std::ofstream f(p);
    f << s;
}

// REPORTES DE DISCO: MBR y DISK
std::string mbrDot(const FS& fs) {
    std::ifstream f(fs.path, std::ios::binary);
    MBR m{};
    rd(f, 0, m);
    
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];m[label=<<TABLE BORDER='1' CELLBORDER='1'>"
      << "<TR><TD COLSPAN='2'><B>MBR</B></TD></TR>"
      << "<TR><TD>mbr_tamano</TD><TD>" << m.mbr_tamano << "</TD></TR>"
      << "<TR><TD>fecha</TD><TD>" << m.mbr_fecha_creacion << "</TD></TR>"
      << "<TR><TD>signature</TD><TD>" << m.mbr_dsk_signature << "</TD></TR>"
      << "<TR><TD>fit</TD><TD>" << m.dsk_fit << "</TD></TR>";
      
    for (int i = 0; i < 4; i++) {
        auto& p = m.mbr_partition[i];
        o << "<TR><TD>PART " << i << "</TD><TD>status=" << p.part_status
          << " type=" << p.part_type << " fit=" << p.part_fit
          << " start=" << p.part_start << " size=" << p.part_s
          << " name=" << esc(p.part_name) << " correlativ=" << p.part_correlativ
          << " id=" << esc(std::string(p.part_id, 4)) << "</TD></TR>";
    }
    o << "</TABLE>>];";
    
    int ei = -1;
    for (int i = 0; i < 4; i++) {
        if (m.mbr_partition[i].part_type == 'e') ei = i;
    }
    
    if (ei >= 0) {
        int pos = m.mbr_partition[ei].part_start;
        int end = pos + m.mbr_partition[ei].part_s;
        int n = 0;
        
        while (pos >= m.mbr_partition[ei].part_start && pos + static_cast<int>(sizeof(EBR)) <= end) {
            EBR e{};
            if (!leerEBR(fs.path, pos, e)) break;
            if (e.part_s <= 0) break;
            
            o << "e" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
              << "<TR><TD COLSPAN='2'><B>EBR " << n << "</B></TD></TR>"
              << "<TR><TD>start</TD><TD>" << e.part_start << "</TD></TR>"
              << "<TR><TD>size</TD><TD>" << e.part_s << "</TD></TR>"
              << "<TR><TD>next</TD><TD>" << e.part_next << "</TD></TR>"
              << "<TR><TD>name</TD><TD>" << esc(e.part_name) << "</TD></TR></TABLE>>];m->e" << n << ";";
              
            if (e.part_next == -1) break;
            pos = e.part_next;
            n++;
            if (n > 128) break;
        }
    }
    o << "}";
    return o.str();
}

std::string diskDot(const FS& fs) {
    std::ifstream f(fs.path, std::ios::binary);
    MBR m{};
    rd(f, 0, m);
    
    std::vector<std::pair<int, int>> parts;
    for (int i = 0; i < 4; i++) {
        if (m.mbr_partition[i].part_s > 0) {
            parts.push_back({m.mbr_partition[i].part_start, m.mbr_partition[i].part_start + m.mbr_partition[i].part_s});
        }
    }
    std::sort(parts.begin(), parts.end());
    
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];d[label=<<TABLE BORDER='1' CELLBORDER='1'>"
      << "<TR><TD>MBR<br/>" << std::fixed << std::setprecision(2) << 100.0 * sizeof(MBR) / m.mbr_tamano << "%</TD>";
      
    int cur = sizeof(MBR);
    for (auto& x : parts) {
        if (x.first > cur) {
            o << "<TD>LIBRE<br/>" << std::fixed << std::setprecision(2) << 100.0 * (x.first - cur) / m.mbr_tamano << "%</TD>";
        }
        o << "<TD>";
        for (int i = 0; i < 4; i++) {
            if (m.mbr_partition[i].part_start == x.first) {
                o << m.mbr_partition[i].part_name << "<br/>"
                  << (m.mbr_partition[i].part_type == 'e' ? "EXTENDIDA" : "PRIMARIA") << "<br/>"
                  << std::fixed << std::setprecision(2) << 100.0 * (x.second - x.first) / m.mbr_tamano << "%";
            }
        }
        o << "</TD>";
        cur = x.second;
    }
    
    if (cur < m.mbr_tamano) {
        o << "<TD>LIBRE<br/>" << 100.0 * (m.mbr_tamano - cur) / m.mbr_tamano << "%</TD>";
    }
    o << "</TR></TABLE>>];}";
    return o.str();
}

// REPORTE DE INODOS: INODE
std::string inodeDot(const FS& fs) {
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];";
    
    for (int n = 0; n < fs.sb.s_inodes_count; n++) {
        inodo i{};
        if (!inode(fs, n, i)) continue;
        
        std::ifstream b(fs.path, std::ios::binary);
        b.seekg(fs.sb.s_bm_inode_start + n);
        char used = 0;
        b.read(&used, 1);
        if (used != 1) continue;
        
        o << "i" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
          << "<TR><TD COLSPAN='2'>INODO " << n << "</TD></TR>"
          << "<TR><TD>uid</TD><TD>" << i.i_uid << "</TD></TR>"
          << "<TR><TD>gid</TD><TD>" << i.i_gid << "</TD></TR>"
          << "<TR><TD>size</TD><TD>" << i.i_s << "</TD></TR>"
          << "<TR><TD>type</TD><TD>" << (i.i_type == 0 ? "CARPETA" : "ARCHIVO") << "</TD></TR>"
          << "<TR><TD>perm</TD><TD>" << i.i_perm << "</TD></TR>";
          
        for (int j = 0; j < 15; j++) {
            o << "<TR><TD>block[" << j << "]</TD><TD>" << i.i_block[j] << "</TD></TR>";
        }
        o << "</TABLE>>];";
    }
    o << "}";
    return o.str();
}

// REPORTE DE BLOQUES: BLOCK
std::string blockDot(const FS& fs) {
    std::set<int> folderBlocks, fileBlocks, ptrBlocks;
    
    for (int n = 0; n < fs.sb.s_inodes_count; n++) {
        std::ifstream bm(fs.path, std::ios::binary);
        bm.seekg(fs.sb.s_bm_inode_start + n);
        char used = 0;
        bm.read(&used, 1);
        if (used != 1) continue;
        
        inodo i{};
        if (!inode(fs, n, i)) continue;
        
        for (int b : i.i_block) {
            if (b >= 0) {
                if (i.i_type == 0) folderBlocks.insert(b);
                else fileBlocks.insert(b);
            }
        }
    }
    
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];";
    
    for (int n = 0; n < fs.sb.s_blocks_count; n++) {
        std::ifstream bm(fs.path, std::ios::binary);
        bm.seekg(fs.sb.s_bm_block_start + n);
        char used = 0;
        bm.read(&used, 1);
        if (used != 1) continue;
        
        if (folderBlocks.count(n)) {
            bcarpetas c{};
            if (!blockFolder(fs, n, c)) continue;
            o << "b" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
              << "<TR><TD COLSPAN='2'>BLOQUE CARPETA " << n << "</TD></TR>";
            for (auto& e : c.b_content) {
                o << "<TR><TD>" << esc(e.b_name) << "</TD><TD>" << e.b_inodo << "</TD></TR>";
            }
            o << "</TABLE>>];";
        } else if (fileBlocks.count(n)) {
            barchivos a{};
            if (!blockFile(fs, n, a)) continue;
            o << "b" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
              << "<TR><TD>BLOQUE ARCHIVO " << n << "</TD></TR>"
              << "<TR><TD>" << esc(std::string(a.b_content, 64)) << "</TD></TR></TABLE>>];";
        } else {
            o << "b" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
              << "<TR><TD>BLOQUE APUNTADORES " << n << "</TD></TR></TABLE>>];";
        }
    }
    o << "}";
    return o.str();
}

// REPORTE DE SUPERBLOQUE: SB
std::string sbDot(const FS& fs) {
    const auto& s = fs.sb;
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];s[label=<<TABLE BORDER='1' CELLBORDER='1'><TR><TD COLSPAN='2'>SUPERBLOQUE</TD></TR>";
    
    auto row = [&](const char* n, long long v) {
        o << "<TR><TD>" << n << "</TD><TD>" << v << "</TD></TR>";
    };
    
    row("s_filesystem_type", s.s_filesystem_type);
    row("s_inodes_count", s.s_inodes_count);
    row("s_blocks_count", s.s_blocks_count);
    row("s_free_blocks_count", s.s_free_blocks_count);
    row("s_free_inodes_count", s.s_free_inodes_count);
    row("s_mtime", s.s_mtime);
    row("s_umtime", s.s_umtime);
    row("s_mnt_count", s.s_mnt_count);
    row("s_magic", s.s_magic);
    row("s_inode_s", s.s_inode_s);
    row("s_block_s", s.s_block_s);
    row("s_first_ino", s.s_first_ino);
    row("s_first_blo", s.s_first_blo);
    row("s_bm_inode_start", s.s_bm_inode_start);
    row("s_bm_block_start", s.s_bm_block_start);
    row("s_inode_start", s.s_inode_start);
    row("s_block_start", s.s_block_start);
    
    o << "</TABLE>>];}";
    return o.str();
}

// REPORTE DE ARBOL EXT2: TREE
std::string treeDot(const FS& fs) {
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];";
    std::set<int> seenI, seenB;
    
    std::function<void(int)> walk = [&](int n) {
        if (n < 0 || seenI.count(n)) return;
        seenI.insert(n);
        
        inodo i{};
        if (!inode(fs, n, i)) return;
        
        o << "i" << n << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
          << "<TR><TD>INODO " << n << "</TD></TR>"
          << "<TR><TD>" << (i.i_type == 0 ? "CARPETA" : "ARCHIVO")
          << " size=" << i.i_s << " perm=" << i.i_perm << "</TD></TR></TABLE>>];";
          
        for (int b : i.i_block) {
            if (b < 0) continue;
            o << "i" << n << "->b" << b << ";";
            if (seenB.count(b)) continue;
            seenB.insert(b);
            
            if (i.i_type == 0) {
                bcarpetas c{};
                if (blockFolder(fs, b, c)) {
                    o << "b" << b << "[label=<<TABLE BORDER='1' CELLBORDER='1'><TR><TD>BLOQUE " << b << "</TD></TR>";
                    for (auto& e : c.b_content) {
                        if (e.b_inodo >= 0) {
                            o << "<TR><TD>" << esc(e.b_name) << " : " << e.b_inodo << "</TD></TR>";
                        }
                    }
                    o << "</TABLE>>];";
                    
                    for (auto& e : c.b_content) {
                        if (e.b_inodo >= 0 && std::string(e.b_name) != "." && std::string(e.b_name) != "..") {
                            walk(e.b_inodo);
                        }
                    }
                }
            } else {
                barchivos a{};
                if (blockFile(fs, b, a)) {
                    o << "b" << b << "[label=<<TABLE BORDER='1' CELLBORDER='1'>"
                      << "<TR><TD>BLOQUE ARCHIVO " << b << "</TD></TR>"
                      << "<TR><TD>" << esc(std::string(a.b_content, 64)) << "</TD></TR></TABLE>>];";
                }
            }
        }
    };
    
    walk(0);
    o << "}";
    return o.str();
}

// REPORTE DE LISTADO: LS
std::string lsDot(const FS& fs, const std::string& path) {
    int n;
    if (!findPath(fs, path, n)) return {};
    
    inodo i{};
    if (!inode(fs, n, i)) return {};
    
    std::ostringstream o;
    o << "digraph G{node[shape=plaintext];l[label=<<TABLE BORDER='1' CELLBORDER='1'>"
      << "<TR><TD>Nombre</TD><TD>Tipo</TD><TD>Perm</TD><TD>UID</TD><TD>GID</TD><TD>Tamano</TD><TD>Creacion</TD><TD>Modificacion</TD></TR>";
      
    auto row = [&](const std::string& name, int ino) {
        inodo x{};
        if (!inode(fs, ino, x)) return;
        o << "<TR><TD>" << esc(name) << "</TD><TD>"
          << (x.i_type == 0 ? "carpeta" : "archivo") << "</TD><TD>"
          << x.i_perm << "</TD><TD>" << x.i_uid << "</TD><TD>"
          << x.i_gid << "</TD><TD>" << x.i_s << "</TD><TD>"
          << std::ctime(&x.i_ctime) << "</TD><TD>"
          << std::ctime(&x.i_mtime) << "</TD></TR>";
    };
    
    if (n == 0) row("/", 0);
    
    for (int b : i.i_block) {
        if (b < 0) continue;
        bcarpetas c{};
        if (!blockFolder(fs, b, c)) continue;
        for (auto& e : c.b_content) {
            if (e.b_inodo >= 0 && std::string(e.b_name) != "." && std::string(e.b_name) != "..") {
                row(e.b_name, e.b_inodo);
            }
        }
    }
    o << "</TABLE>>];}";
    return o.str();
}

} // namespace

void rep(std::vector<std::string>& p, EstadoGlobal& estado) {
    std::string name, path, id, filels = "/";
    
    for (size_t i = 1; i < p.size(); ++i) {
        std::string k, v;
        if (!split(p[i], k, v)) {
            std::cout << "[ERROR] REP requiere parametros clave=valor.\n";
            return;
        }
        if (k == "name") name = low(v);
        else if (k == "path") path = v;
        else if (k == "id") id = v;
        else if (k == "path_file_ls") filels = v;
        else {
            std::cout << "[ERROR] Parametro no reconocido en REP: -" << k << "\n";
            return;
        }
    }
    
    std::set<std::string> ok = {"mbr", "disk", "inode", "block", "bm_inode", "bm_block", "bm_bloc", "tree", "sb", "file", "ls"};
    if (ok.count(name) == 0) {
        std::cout << "[ERROR] REP -name invalido.\n";
        return;
    }
    
    if (path.empty() || id.empty()) {
        std::cout << "[ERROR] REP requiere -name, -path e -id.\n";
        return;
    }

    // -id identifica la particion montada para los reportes del sistema de archivos.
    DiskCtx dctx;
    if (!diskFromId(estado, id, dctx)) {
        std::cout << "[ERROR] El ID no existe o no esta montado.\n";
        return;
    }
    
    FS fs;
    if (name != "mbr" && name != "disk" && !fsFrom(estado, id, fs)) {
        std::cout << "[ERROR] La particion indicada no esta formateada como EXT2.\n";
        return;
    }

    // REPORTES DE BITMAPS: BM_INODE / BM_BLOCK
    std::filesystem::path out(path);
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    if (ec) {
        std::cout << "[ERROR] No se pudo crear la carpeta del reporte: " << ec.message() << "\n";
        return;
    }
    
    std::string dot = path + ".dot";
    
    if (name == "bm_inode" || name == "bm_block" || name == "bm_bloc") {
        std::ifstream f(fs.path, std::ios::binary);
        std::ostringstream o;
        int start = (name == "bm_inode") ? fs.sb.s_bm_inode_start : fs.sb.s_bm_block_start;
        int count = (name == "bm_inode") ? fs.sb.s_inodes_count : fs.sb.s_blocks_count;
        
        for (int i = 0; i < count; i++) {
            f.seekg(start + i);
            char c = 0;
            f.read(&c, 1);
            o << (c == 1 ? '1' : '0');
            if ((i + 1) % 20 == 0) o << '\n';
            else o << ' ';
        }
        
        writeText(path, o.str());
        std::cout << "[EXITOSO] Reporte " << name << " generado: " << path << "\n";
        return;
    }

    // REPORTE DE ARCHIVO: FILE
    // FILE y LS son los unicos reportes que requieren una sesion activa.
    if (name == "file") {
        if (!estado.sesion.activa) {
            std::cout << "[ERROR] REP -file requiere una sesion activa.\n";
            return;
        }
        int n;
        if (!findPath(fs, filels, n)) {
            std::cout << "[ERROR] No existe -path_file_ls: " << filels << "\n";
            return;
        }
        inodo i{};
        if (!inode(fs, n, i) || i.i_type != 1) {
            std::cout << "[ERROR] La ruta no corresponde a un archivo.\n";
            return;
        }
        writeText(path, contentOf(fs, i));
        std::cout << "[EXITOSO] Reporte file generado: " << path << "\n";
        return;
    }

    // Genera el reporte grafico
    std::string d;
    if (name == "mbr") {
        FS diskFs;
        diskFs.path = dctx.path;
        d = mbrDot(diskFs);
    } else if (name == "disk") {
        FS diskFs;
        diskFs.path = dctx.path;
        d = diskDot(diskFs);
    } else if (name == "inode") {
        d = inodeDot(fs);
    } else if (name == "block") {
        d = blockDot(fs);
    } else if (name == "tree") {
        d = treeDot(fs);
    } else if (name == "sb") {
        d = sbDot(fs);
    } else if (name == "ls") {
        if (!estado.sesion.activa) {
            std::cout << "[ERROR] REP -ls requiere una sesion activa.\n";
            return;
        }
        d = lsDot(fs, filels);
        if (d.empty()) {
            std::cout << "[ERROR] No existe -path_file_ls: " << filels << "\n";
            return;
        }
    }

    std::ofstream df(dot);
    if (!df) {
        std::cout << "[ERROR] No se pudo crear el archivo temporal del reporte: " << dot << "\n";
        return;
    }
    df << d;
    df.close();

    if (!dotRender(dot, path)) {
        std::cout << "[ERROR] No se genero el reporte " << path << ".\n";
        return;
    }

    std::cout << "[EXITOSO] Reporte " << name << " generado: " << path << "\n";
}