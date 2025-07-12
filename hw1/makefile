# Derleyici
CC = gcc
# Derleme seçenekleri
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE
# Çıktı dosya adı
TARGET = main
# Kaynak ve nesne dosyaları
SRC = main.c
OBJ = main.o

# Varsayılan hedef

all: $(TARGET)
	./$(TARGET)


# Nesne dosyasını derle
main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

# Çalıştırılabilir dosyayı oluştur
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

# Temizlik komutu
clean:
	rm -f $(OBJ) $(TARGET)    

# Zorla yeniden derleme (make rebuild komutu için)
rebuild: clean all
