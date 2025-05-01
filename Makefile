# Nombre del ejecutable
TARGET = twitch_irc

# Directorios
SRC_DIR = src
BUILD_DIR = build

# Compilador y opciones
CC = g++
CFLAGS = -O3 -Wall -g
LDFLAGS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -pthread
ARCHFLAGS = -march=native -flto=2 -msse2 # CPU capabilites options
# Archivos fuente y objetos
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

# Regla por defecto
all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET)

# Crear directorio build si no existe
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Regla para enlazar el ejecutable
$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Regla para compilar archivos fuente a objetos
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CC) $(CFLAGS) $(ARCHFLAGS) -c $< -o $@

# Limpiar archivos generados
clean:
	rm -rf $(BUILD_DIR)

# Recompilar todo desde cero
rebuild: clean all

# Evitar conflictos con nombres de archivos
.PHONY: all clean rebuild
