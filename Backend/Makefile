# Makefile - C++ DISK ONLINE
#
# make          Compila la consola y el servidor HTTP.
# make consola  Compila únicamente el analizador de consola.
# make api      Compila únicamente la API HTTP.
# make test     Ejecuta las pruebas funcionales y de integración HTTP.
# make clean    Elimina objetos y ejecutables generados.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra
API_FLAGS ?= -DAPI_MODE
BUILD_DIR := build

# Solo estos dos módulos cambian su comportamiento en API_MODE porque
# no deben solicitar confirmaciones interactivas desde stdin.
COMUNES_API_VARIANTES = fdisk.cpp mkfile.cpp
COMUNES_COMPARTIDOS = $(filter-out $(COMUNES_API_VARIANTES),\
    analizador.cpp structs.cpp validaciones.cpp disk_utils.cpp mkdisk.cpp \
    rmdisk.cpp mount.cpp mounted.cpp mkfs.cpp ext2_utils.cpp journaling.cpp \
    journal_command.cpp mkusr.cpp rmusr.cpp mkdir.cpp cat.cpp login.cpp \
    logout.cpp mkgrp.cpp rmgrp.cpp chgrp.cpp unmount.cpp remove.cpp rename.cpp \
    copy.cpp move.cpp find.cpp chown.cpp chmod.cpp loss.cpp reports.cpp)

API_ESPECIFICOS = api_server_main.cpp api_filesystem.cpp http_server.cpp json_util.cpp
OBJ_COMPARTIDOS = $(patsubst %.cpp,$(BUILD_DIR)/comunes/%.o,$(COMUNES_COMPARTIDOS))
OBJ_CONSOLA = $(BUILD_DIR)/consola/main.o $(OBJ_COMPARTIDOS) \
    $(BUILD_DIR)/consola/fdisk.o $(BUILD_DIR)/consola/mkfile.o
OBJ_API = $(patsubst %.cpp,$(BUILD_DIR)/api/%.o,$(API_ESPECIFICOS)) \
    $(OBJ_COMPARTIDOS) $(BUILD_DIR)/api/fdisk.o $(BUILD_DIR)/api/mkfile.o

.PHONY: all consola api run-api run-consola test clean

all: consola api

consola: analizador_ext2
api: api_server

analizador_ext2: $(OBJ_CONSOLA)
	$(CXX) $(CXXFLAGS) $^ -o $@

api_server: $(OBJ_API)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/consola/main.o: main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/comunes/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/consola/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/api/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(API_FLAGS) -MMD -MP -c $< -o $@

run-consola: analizador_ext2
	./analizador_ext2

run-api: api_server
	./api_server


clean:
	rm -rf $(BUILD_DIR) analizador_ext2 api_server

-include $(OBJ_CONSOLA:.o=.d) $(OBJ_API:.o=.d) $(OBJ_COMPARTIDOS:.o=.d)
