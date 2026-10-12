#include "ext2_utils.h"
#include "disk_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <set>
#include <ctime>

namespace {
template<class T> bool rd(const std::string&p,std::streamoff off,T&x){std::ifstream f(p,std::ios::binary);if(!f)return false;f.seekg(off);f.read(reinterpret_cast<char*>(&x),sizeof(T));return bool(f);}
template<class T> bool wr(const std::string&p,std::streamoff off,const T&x){std::fstream f(p,std::ios::in|std::ios::out|std::ios::binary);if(!f)return false;f.seekp(off);f.write(reinterpret_cast<const char*>(&x),sizeof(T));f.flush();return bool(f);}
int permBits(const inodo&i,int uid,int gid){if(uid==1)return 7; int p=i.i_perm; int oct[3]={p/100,(p/10)%10,p%10}; return uid==i.i_uid?oct[0]:(gid==i.i_gid?oct[1]:oct[2]);}
void initInode(inodo&i){std::memset(&i,0,sizeof(i));for(int&b:i.i_block)b=-1;}
void initFolder(bcarpetas&b){std::memset(&b,0,sizeof(b));for(auto &c:b.b_content)c.b_inodo=-1;}
std::string trim(std::string s){while(!s.empty()&&std::isspace((unsigned char)s.front()))s.erase(s.begin());while(!s.empty()&&std::isspace((unsigned char)s.back()))s.pop_back();return s;}
std::vector<std::string> splitComma(const std::string&s){std::vector<std::string>v;std::stringstream ss(s);std::string x;while(std::getline(ss,x,','))v.push_back(trim(x));return v;}
std::string joinUsers(const std::vector<UserRecord>&r){std::ostringstream o;for(auto &x:r){o<<x.id<<", "<<x.type<<", "<<x.group;if(x.type=='U')o<<", "<<x.user<<", "<<x.pass;o<<"\n";}return o.str();}
bool blockIndex(const Ext2Context&c,const inodo&i,int logical,int &physical){
 if (logical < 0) return false;
 if (logical < 12) {
     physical = i.i_block[logical];
     return physical >= 0;
 }
 logical-=12; int per=16;
 if(logical<per && i.i_block[12]>=0){bapuntadores p{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)i.i_block[12]*sizeof(bapuntadores),p))return false;physical=p.b_pointers[logical];return physical>=0;}
 logical-=per; if(logical<per*per && i.i_block[13]>=0){bapuntadores a{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)i.i_block[13]*sizeof(bapuntadores),a))return false;int x=logical/per,y=logical%per;if(a.b_pointers[x]<0)return false;bapuntadores b{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)a.b_pointers[x]*sizeof(bapuntadores),b))return false;physical=b.b_pointers[y];return physical>=0;}
 logical-=per*per; if(logical<per*per*per && i.i_block[14]>=0){bapuntadores a{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)i.i_block[14]*sizeof(bapuntadores),a))return false;int x=logical/(per*per), rem=logical%(per*per), y=rem/per,z=rem%per;if(a.b_pointers[x]<0)return false;bapuntadores b{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)a.b_pointers[x]*sizeof(bapuntadores),b))return false;if(b.b_pointers[y]<0)return false;bapuntadores d{};if(!rd(c.path,c.sb.s_block_start+(std::streamoff)b.b_pointers[y]*sizeof(bapuntadores),d))return false;physical=d.b_pointers[z];return physical>=0;} return false;
}
bool setBlockIndex(Ext2Context &ctx, inodo &inode, int logical, int physical) {
    if (logical < 0 || physical < 0) return false;

    constexpr int P = 16;
    constexpr int P2 = P * P;
    constexpr int P3 = P2 * P;
    const std::streamoff base = ctx.sb.s_block_start;

    auto reservarTabla = [&](int &indice) -> bool {
        if (indice >= 0) return true;

        indice = asignarBloque(ctx);
        if (indice < 0) return false;

        bapuntadores tabla{};
        for (int &puntero : tabla.b_pointers) puntero = -1;
        if (!wr(ctx.path, base + static_cast<std::streamoff>(indice) * sizeof(tabla), tabla)) {
            liberarBloque(ctx, indice);
            indice = -1;
            return false;
        }
        return true;
    };

    auto leerTabla = [&](int indice, bapuntadores &tabla) -> bool {
        return indice >= 0 && rd(
            ctx.path,
            base + static_cast<std::streamoff>(indice) * sizeof(tabla),
            tabla);
    };

    auto guardarTabla = [&](int indice, const bapuntadores &tabla) -> bool {
        return indice >= 0 && wr(
            ctx.path,
            base + static_cast<std::streamoff>(indice) * sizeof(tabla),
            tabla);
    };

    if (logical < 12) {
        inode.i_block[logical] = physical;
        return true;
    }

    logical -= 12;
    if (logical < P) {
        if (!reservarTabla(inode.i_block[12])) return false;

        bapuntadores tabla{};
        if (!leerTabla(inode.i_block[12], tabla)) return false;
        tabla.b_pointers[logical] = physical;
        return guardarTabla(inode.i_block[12], tabla);
    }

    logical -= P;
    if (logical < P2) {
        if (!reservarTabla(inode.i_block[13])) return false;

        bapuntadores raiz{};
        if (!leerTabla(inode.i_block[13], raiz)) return false;
        const int indiceMedio = logical / P;
        const int indiceDato = logical % P;

        if (!reservarTabla(raiz.b_pointers[indiceMedio])) return false;
        if (!guardarTabla(inode.i_block[13], raiz)) return false;

        bapuntadores medio{};
        if (!leerTabla(raiz.b_pointers[indiceMedio], medio)) return false;
        medio.b_pointers[indiceDato] = physical;
        return guardarTabla(raiz.b_pointers[indiceMedio], medio);
    }

    logical -= P2;
    if (logical >= P3) return false;
    if (!reservarTabla(inode.i_block[14])) return false;

    bapuntadores raiz{};
    if (!leerTabla(inode.i_block[14], raiz)) return false;

    const int indiceSuperior = logical / P2;
    const int resto = logical % P2;
    const int indiceMedio = resto / P;
    const int indiceDato = resto % P;

    if (!reservarTabla(raiz.b_pointers[indiceSuperior])) return false;
    if (!guardarTabla(inode.i_block[14], raiz)) return false;

    bapuntadores medio{};
    if (!leerTabla(raiz.b_pointers[indiceSuperior], medio)) return false;
    if (!reservarTabla(medio.b_pointers[indiceMedio])) return false;
    if (!guardarTabla(raiz.b_pointers[indiceSuperior], medio)) return false;

    bapuntadores hoja{};
    if (!leerTabla(medio.b_pointers[indiceMedio], hoja)) return false;
    hoja.b_pointers[indiceDato] = physical;
    return guardarTabla(medio.b_pointers[indiceMedio], hoja);
}

bool liberarNodosPunteros(
    Ext2Context &ctx,
    int indiceRaiz,
    int nivel,
    std::set<int> &visitados
) {
    if (indiceRaiz < 0 || !visitados.insert(indiceRaiz).second) return true;

    bool correcto = true;
    if (nivel > 1) {
        bapuntadores tabla{};
        if (!leerBloquePunteros(ctx, indiceRaiz, tabla)) return false;

        for (int hijo : tabla.b_pointers) {
            if (hijo >= 0 && !liberarNodosPunteros(ctx, hijo, nivel - 1, visitados)) {
                correcto = false;
            }
        }
    }

    // En nivel 1, las entradas son bloques de datos; esos bloques los libera
    // la pasada que recorre los índices lógicos del archivo.
    if (!liberarBloque(ctx, indiceRaiz)) correcto = false;
    return correcto;
}

bool liberarAlmacenamientoArchivo(Ext2Context &ctx, const inodo &inode, int tamano) {
    bool correcto = true;
    const int cantidadBloques = std::max(0, (tamano + 63) / 64);
    std::set<int> bloquesDatos;

    for (int logico = 0; logico < cantidadBloques; ++logico) {
        int fisico = -1;
        if (blockIndex(ctx, inode, logico, fisico) && fisico >= 0 &&
            bloquesDatos.insert(fisico).second) {
            if (!liberarBloque(ctx, fisico)) correcto = false;
        }
    }

    std::set<int> bloquesPunteros;
    if (inode.i_block[12] >= 0 &&
        !liberarNodosPunteros(ctx, inode.i_block[12], 1, bloquesPunteros)) {
        correcto = false;
    }
    if (inode.i_block[13] >= 0 &&
        !liberarNodosPunteros(ctx, inode.i_block[13], 2, bloquesPunteros)) {
        correcto = false;
    }
    if (inode.i_block[14] >= 0 &&
        !liberarNodosPunteros(ctx, inode.i_block[14], 3, bloquesPunteros)) {
        correcto = false;
    }

    return correcto;
}

} // namespace

bool obtenerContexto(EstadoGlobal &estado,const std::string&id,Ext2Context&ctx){
 disco*d=nullptr;montada*p=nullptr;if(!buscarMontada(estado,id,d,p))return false;ctx.path=d->ruta;
 if(p->posMBR>=0){SimDisk sd;if(!cargarDisco(ctx.path,sd))return false;ctx.start=sd.particiones[p->posMBR].part_start;ctx.size=sd.particiones[p->posMBR].part_s;}
 else {EBR e{};if(!leerEBR(ctx.path,p->posEBR,e))return false;ctx.start=e.part_start;ctx.size=e.part_s;}
 if (!rd(ctx.path, ctx.start, ctx.sb) || ctx.sb.s_magic != EXT2_MAGIC) return false;
 return true;
}
bool sesionActiva(EstadoGlobal&estado,Ext2Context&ctx){return estado.sesion.activa&&obtenerContexto(estado,estado.sesion.id,ctx);}
bool leerInodo(const Ext2Context&c,int n,inodo&i){if(n<0||n>=c.sb.s_inodes_count)return false;return rd(c.path,c.sb.s_inode_start+(std::streamoff)n*sizeof(inodo),i);}
bool escribirInodo(const Ext2Context&c,int n,const inodo&i){return wr(c.path,c.sb.s_inode_start+(std::streamoff)n*sizeof(inodo),i);}
bool leerBloqueCarpeta(const Ext2Context&c,int n,bcarpetas&b){return rd(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(bcarpetas),b);}
bool escribirBloqueCarpeta(const Ext2Context&c,int n,const bcarpetas&b){return wr(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(bcarpetas),b);}
bool leerBloqueArchivo(const Ext2Context&c,int n,barchivos&b){return rd(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(barchivos),b);}
bool escribirBloqueArchivo(const Ext2Context&c,int n,const barchivos&b){return wr(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(barchivos),b);}
bool leerBloquePunteros(const Ext2Context&c,int n,bapuntadores&b){return rd(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(bapuntadores),b);}
bool escribirBloquePunteros(const Ext2Context&c,int n,const bapuntadores&b){return wr(c.path,c.sb.s_block_start+(std::streamoff)n*sizeof(bapuntadores),b);}
bool guardarSuper(const Ext2Context&c){return wr(c.path,c.start,c.sb);}
int asignarInodo(Ext2Context&c){std::fstream f(c.path,std::ios::in|std::ios::out|std::ios::binary);if(!f)return -1;for(int i=0;i<c.sb.s_inodes_count;i++){f.seekg(c.sb.s_bm_inode_start+i);char b=0;f.read(&b,1);if(b==0){b=1;f.seekp(c.sb.s_bm_inode_start+i);f.write(&b,1);c.sb.s_free_inodes_count--;c.sb.s_first_ino=(i+1<c.sb.s_inodes_count)?i+1:i;guardarSuper(c);return i;}}return -1;}
int asignarBloque(Ext2Context&c){std::fstream f(c.path,std::ios::in|std::ios::out|std::ios::binary);if(!f)return -1;for(int i=0;i<c.sb.s_blocks_count;i++){f.seekg(c.sb.s_bm_block_start+i);char b=0;f.read(&b,1);if(b==0){b=1;f.seekp(c.sb.s_bm_block_start+i);f.write(&b,1);c.sb.s_free_blocks_count--;c.sb.s_first_blo=(i+1<c.sb.s_blocks_count)?i+1:i;guardarSuper(c);return i;}}return -1;}
bool liberarInodo(Ext2Context&c,int n){if(n<0||n>=c.sb.s_inodes_count)return false;char z=0;std::fstream f(c.path,std::ios::in|std::ios::out|std::ios::binary);if(!f)return false;f.seekp(c.sb.s_bm_inode_start+n);f.write(&z,1);c.sb.s_free_inodes_count++;guardarSuper(c);return true;}
bool liberarBloque(Ext2Context&c,int n){if(n<0||n>=c.sb.s_blocks_count)return false;char z=0;std::fstream f(c.path,std::ios::in|std::ios::out|std::ios::binary);if(!f)return false;f.seekp(c.sb.s_bm_block_start+n);f.write(&z,1);c.sb.s_free_blocks_count++;guardarSuper(c);return true;}
std::string normalizarRuta(const std::string&p){if(p.empty())return "/";std::string r=p;if(r[0]!='/')r="/"+r;while(r.size()>1&&r.back()=='/')r.pop_back();return r;}
std::vector<std::string> partesRuta(const std::string&p){std::vector<std::string>v;std::stringstream ss(normalizarRuta(p));std::string x;while(std::getline(ss,x,'/'))if(!x.empty())v.push_back(x);return v;}
bool buscarEntrada(const Ext2Context&c,int dir,const std::string&nombre,int&ino,int*bloque,int*slot){inodo d{};if(!leerInodo(c,dir,d)||d.i_type!=0)return false;for(int k=0;k<15;k++){int pb;if(!blockIndex(c,d,k,pb))continue;bcarpetas b{};if(!leerBloqueCarpeta(c,pb,b))continue;for(int s=0;s<4;s++)if(b.b_content[s].b_inodo>=0&&std::string(b.b_content[s].b_name)==nombre){ino=b.b_content[s].b_inodo;if(bloque)*bloque=pb;if(slot)*slot=s;return true;}}return false;}
bool resolverRuta(const Ext2Context&c,const std::string&path,int&ino,int*padre,std::string*nombre){auto v=partesRuta(path);ino=0;int par=0;if(v.empty()){if(padre)*padre=-1;if(nombre)*nombre="/";return true;}for(auto&x:v){par=ino;if(!buscarEntrada(c,ino,x,ino))return false;}if(padre)*padre=par;if(nombre)*nombre=v.back();return true;}

bool esDescendienteDirectorio(const Ext2Context &ctx, int posibleAncestro, int directorioDestino) {
    if (posibleAncestro < 0 || directorioDestino < 0) return false;

    int actual = directorioDestino;
    std::set<int> visitados;

    // Se recorre la cadena de padres real, no solo el texto de la ruta. Esto
    // también detecta alias de ruta que incluyen componentes "..".
    for (int pasos = 0; pasos < ctx.sb.s_inodes_count; ++pasos) {
        if (actual == posibleAncestro) return true;
        if (!visitados.insert(actual).second) return false;

        inodo directorio{};
        if (!leerInodo(ctx, actual, directorio) || directorio.i_type != 0) return false;

        int padre = -1;
        if (!buscarEntrada(ctx, actual, "..", padre)) return false;
        if (padre == actual) return false; // Se llego a la raiz.
        actual = padre;
    }

    return false;
}
bool agregarEntrada(Ext2Context&c,int dir,const std::string&nombre,int child){if(nombre.empty()||nombre.size()>11)return false;inodo d{};if(!leerInodo(c,dir,d)||d.i_type!=0)return false;if(permBits(d,1,1)==0)return false;for(int k=0;k<15;k++){int pb;if(blockIndex(c,d,k,pb)){bcarpetas b{};leerBloqueCarpeta(c,pb,b);for(int s=0;s<4;s++)if(b.b_content[s].b_inodo<0){std::memset(b.b_content[s].b_name,0,12);std::strncpy(b.b_content[s].b_name,nombre.c_str(),11);b.b_content[s].b_inodo=child;return escribirBloqueCarpeta(c,pb,b);}}else{int nb=asignarBloque(c);if(nb<0)return false;bcarpetas b{};initFolder(b);std::strncpy(b.b_content[0].b_name,nombre.c_str(),11);b.b_content[0].b_inodo=child;if(!escribirBloqueCarpeta(c,nb,b))return false;if(!setBlockIndex(c,d,k,nb))return false;return escribirInodo(c,dir,d);}}return false;}
bool eliminarEntrada(Ext2Context&c,int dir,const std::string&nombre){int ino,b,slot;if(!buscarEntrada(c,dir,nombre,ino,&b,&slot))return false;bcarpetas x{};if(!leerBloqueCarpeta(c,b,x))return false;x.b_content[slot].b_inodo=-1;std::memset(x.b_content[slot].b_name,0,12);return escribirBloqueCarpeta(c,b,x);}

bool listarEntradasDirectorio(const Ext2Context &c, int dirIno,
                              std::vector<std::pair<std::string, int>> &entradas) {
    entradas.clear();
    inodo directorio{};
    if (!leerInodo(c, dirIno, directorio) || directorio.i_type != 0) return false;

    std::set<int> bloquesVistos;
    for (int indice = 0; indice < 15; ++indice) {
        int bloqueFisico = -1;
        if (!blockIndex(c, directorio, indice, bloqueFisico) || bloqueFisico < 0 ||
            !bloquesVistos.insert(bloqueFisico).second) continue;

        bcarpetas bloque{};
        if (!leerBloqueCarpeta(c, bloqueFisico, bloque)) return false;
        for (const auto &entrada : bloque.b_content) {
            if (entrada.b_inodo < 0 || entrada.b_name[0] == '\0') continue;
            std::size_t longitud = 0;
            while (longitud < sizeof(entrada.b_name) && entrada.b_name[longitud] != '\0') ++longitud;
            entradas.emplace_back(std::string(entrada.b_name, longitud), entrada.b_inodo);
        }
    }
    return true;
}

bool cambiarNombreEntrada(const Ext2Context &c, int dirIno,
                          const std::string &nombreActual, const std::string &nombreNuevo) {
    if (nombreNuevo.empty() || nombreNuevo.size() > 11) return false;
    int ino = -1, bloqueFisico = -1, slot = -1;
    if (!buscarEntrada(c, dirIno, nombreActual, ino, &bloqueFisico, &slot)) return false;

    int existente = -1;
    if (buscarEntrada(c, dirIno, nombreNuevo, existente) && nombreNuevo != nombreActual) return false;

    bcarpetas bloque{};
    if (!leerBloqueCarpeta(c, bloqueFisico, bloque)) return false;
    std::memset(bloque.b_content[slot].b_name, 0, sizeof(bloque.b_content[slot].b_name));
    std::memcpy(bloque.b_content[slot].b_name, nombreNuevo.data(), nombreNuevo.size());
    return escribirBloqueCarpeta(c, bloqueFisico, bloque);
}

bool actualizarPadreDirectorio(const Ext2Context &c, int dirIno, int nuevoPadre) {
    int bloqueFisico = -1, slot = -1, padreViejo = -1;
    if (!buscarEntrada(c, dirIno, "..", padreViejo, &bloqueFisico, &slot)) return false;
    bcarpetas bloque{};
    if (!leerBloqueCarpeta(c, bloqueFisico, bloque)) return false;
    bloque.b_content[slot].b_inodo = nuevoPadre;
    return escribirBloqueCarpeta(c, bloqueFisico, bloque);
}

namespace {
bool liberarBloqueIndirecto(Ext2Context &c, int indice, int nivel, std::set<int> &vistos) {
    if (indice < 0 || !vistos.insert(indice).second) return true;
    bapuntadores apuntadores{};
    if (!leerBloquePunteros(c, indice, apuntadores)) return false;
    bool ok = true;
    for (int hijo : apuntadores.b_pointers) {
        if (hijo < 0) continue;
        if (nivel == 1) {
            if (!liberarBloque(c, hijo)) ok = false;
        } else if (!liberarBloqueIndirecto(c, hijo, nivel - 1, vistos)) {
            ok = false;
        }
    }
    if (!liberarBloque(c, indice)) ok = false;
    return ok;
}

bool liberarArbolRecursivo(Ext2Context &c, int inoNum, std::set<int> &inodosVistos) {
    if (inoNum < 0 || !inodosVistos.insert(inoNum).second) return true;
    inodo actual{};
    if (!leerInodo(c, inoNum, actual)) return false;

    bool ok = true;
    if (actual.i_type == 0) {
        std::vector<std::pair<std::string, int>> entradas;
        if (!listarEntradasDirectorio(c, inoNum, entradas)) return false;
        for (const auto &entrada : entradas) {
            if (entrada.first == "." || entrada.first == "..") continue;
            if (!liberarArbolRecursivo(c, entrada.second, inodosVistos)) ok = false;
        }
    }

    std::set<int> bloquesVistos;
    for (int i = 0; i < 12; ++i) {
        int bloque = actual.i_block[i];
        if (bloque >= 0 && bloquesVistos.insert(bloque).second && !liberarBloque(c, bloque)) ok = false;
    }
    if (actual.i_block[12] >= 0 && !liberarBloqueIndirecto(c, actual.i_block[12], 1, bloquesVistos)) ok = false;
    if (actual.i_block[13] >= 0 && !liberarBloqueIndirecto(c, actual.i_block[13], 2, bloquesVistos)) ok = false;
    if (actual.i_block[14] >= 0 && !liberarBloqueIndirecto(c, actual.i_block[14], 3, bloquesVistos)) ok = false;

    if (!liberarInodo(c, inoNum)) ok = false;
    inodo vacio{};
    std::memset(&vacio, 0, sizeof(vacio));
    for (int &bloque : vacio.i_block) bloque = -1;
    if (!escribirInodo(c, inoNum, vacio)) ok = false;
    return ok;
}
} // namespace

bool liberarArbolInodo(Ext2Context &c, int inoNum) {
    if (inoNum <= 1) return false; // nunca liberar la raiz ni users.txt del sistema.
    std::set<int> vistos;
    return liberarArbolRecursivo(c, inoNum, vistos);
}
bool crearDirectorio(Ext2Context&c,int padre,const std::string&nombre,int uid,int gid,int&nuevo){nuevo=asignarInodo(c);if(nuevo<0)return false;int b=asignarBloque(c);if(b<0){liberarInodo(c,nuevo);return false;}inodo d{};initInode(d);d.i_uid=uid;d.i_gid=gid;d.i_s=0;d.i_atime=d.i_ctime=d.i_mtime=std::time(nullptr);d.i_block[0]=b;d.i_type=0;d.i_perm=664;bcarpetas fb{};initFolder(fb);std::strncpy(fb.b_content[0].b_name,".",11);fb.b_content[0].b_inodo=nuevo;std::strncpy(fb.b_content[1].b_name,"..",11);fb.b_content[1].b_inodo=padre;if(!escribirBloqueCarpeta(c,b,fb)||!escribirInodo(c,nuevo,d)||!agregarEntrada(c,padre,nombre,nuevo)){liberarBloque(c,b);liberarInodo(c,nuevo);return false;}return true;}
bool crearArchivo(Ext2Context&c,int padre,const std::string&nombre,const std::string&contenido,int uid,int gid,int&nuevo){nuevo=asignarInodo(c);if(nuevo<0)return false;inodo a{};initInode(a);a.i_uid=uid;a.i_gid=gid;a.i_s=(int)contenido.size();a.i_atime=a.i_ctime=a.i_mtime=std::time(nullptr);a.i_type=1;a.i_perm=664;if(!escribirInodo(c,nuevo,a)||!escribirArchivoCompleto(c,nuevo,contenido)||!agregarEntrada(c,padre,nombre,nuevo)){liberarInodo(c,nuevo);return false;}return true;}
std::string leerArchivo(const Ext2Context&c,const inodo&i){std::string r;int blocks=(i.i_s+63)/64;for(int k=0;k<blocks;k++){int pb;if(!blockIndex(c,i,k,pb))break;barchivos b{};if(!leerBloqueArchivo(c,pb,b))break;int n=std::min(64,i.i_s-(int)r.size());if(n>0)r.append(b.b_content,n);}return r;}
bool escribirArchivoCompleto(Ext2Context &ctx, int inoNum, const std::string &contenido) {
    if (contenido.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    inodo original{};
    if (!leerInodo(ctx, inoNum, original) || original.i_type != 1) return false;

    // Primero se construye la nueva versión sin liberar la anterior. Así, si
    // falta espacio o falla una escritura, el archivo anterior sigue intacto.
    inodo nuevo = original;
    for (int &bloque : nuevo.i_block) bloque = -1;
    nuevo.i_s = 0;

    auto descartarNuevaVersion = [&](int bytesEscritos) {
        liberarAlmacenamientoArchivo(ctx, nuevo, bytesEscritos);
    };

    for (int offset = 0; offset < static_cast<int>(contenido.size()); offset += 64) {
        const int bloqueFisico = asignarBloque(ctx);
        if (bloqueFisico < 0) {
            descartarNuevaVersion(offset);
            return false;
        }

        barchivos bloque{};
        std::memset(&bloque, 0, sizeof(bloque));
        const int cantidad = std::min(64, static_cast<int>(contenido.size()) - offset);
        std::memcpy(bloque.b_content, contenido.data() + offset,
                    static_cast<std::size_t>(cantidad));

        if (!escribirBloqueArchivo(ctx, bloqueFisico, bloque)) {
            liberarBloque(ctx, bloqueFisico);
            descartarNuevaVersion(offset);
            return false;
        }

        if (!setBlockIndex(ctx, nuevo, offset / 64, bloqueFisico)) {
            liberarBloque(ctx, bloqueFisico);
            descartarNuevaVersion(offset);
            return false;
        }
    }

    nuevo.i_s = static_cast<int>(contenido.size());
    nuevo.i_mtime = std::time(nullptr);
    if (!escribirInodo(ctx, inoNum, nuevo)) {
        descartarNuevaVersion(static_cast<int>(contenido.size()));
        return false;
    }

    // La nueva versión ya está enlazada desde el inodo; ahora se pueden liberar
    // los datos y tablas de punteros de la versión que se reemplazó.
    return liberarAlmacenamientoArchivo(ctx, original, original.i_s);
}

std::vector<UserRecord> leerUsers(const Ext2Context&c){int n;if(!resolverRuta(c,"/users.txt",n))return {};inodo i{};if(!leerInodo(c,n,i))return {};std::vector<UserRecord>r;std::stringstream ss(leerArchivo(c,i));std::string line;while(std::getline(ss,line)){auto v=splitComma(line);if(v.size()>=3){UserRecord u{};try{u.id=std::stoi(v[0]);}catch(...){continue;}u.type=v[1].empty()?' ':v[1][0];u.group=v[2];if(u.type=='U'&&v.size()>=5){u.user=v[3];u.pass=v[4];}r.push_back(u);}}return r;}
bool escribirUsers(Ext2Context&c,const std::vector<UserRecord>&r){int n;if(!resolverRuta(c,"/users.txt",n))return false;return escribirArchivoCompleto(c,n,joinUsers(r));}
int gidActivo(const std::vector<UserRecord>&r,const std::string&g){for(auto&x:r)if(x.id>0&&x.type=='G'&&x.group==g)return x.id;return -1;}
int uidActivo(const std::vector<UserRecord>&r,const std::string&u,const std::string&p){for(auto&x:r)if(x.id>0&&x.type=='U'&&x.user==u&&x.pass==p)return x.id;return -1;}
std::string grupoUsuario(const std::vector<UserRecord>&r,const std::string&u){for(auto&x:r)if(x.id>0&&x.type=='U'&&x.user==u)return x.group;return {};}
bool puedeLeer(const inodo&i,int uid,int gid){return (permBits(i,uid,gid)&4)!=0;}
bool puedeEscribir(const inodo&i,int uid,int gid){return (permBits(i,uid,gid)&2)!=0;}
bool puedeEjecutar(const inodo&i,int uid,int gid){return (permBits(i,uid,gid)&1)!=0;}
