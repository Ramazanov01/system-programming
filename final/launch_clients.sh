# #!/bin/bash

# # Kullanıcı sayısı
# USER_COUNT=15

# # Server IP ve Port (gerekirse değiştir)
# SERVER_IP="127.0.0.1"
# PORT="9000"

# # Chatclient dosyasının bulunduğu yol
# CLIENT_EXEC="./chatclient"

# # Her client'i farklı isimle başlat
# for i in $(seq 1 $USER_COUNT); do
#     (
#         echo "User$i" | $CLIENT_EXEC $SERVER_IP $PORT &
#     ) &
#     sleep 0.2  # Çok aynı anda binmesin, bağlantı çatlayabilir
# done

# echo "[INFO] $USER_COUNT client launched!"

#!/bin/bash

#!/bin/bash

SERVER_IP="127.0.0.1" # Sunucunuzun IP adresi
SERVER_PORT="9000"    # Sunucunuzun dinlediği port
NUM_CLIENTS=15        # Başlatılacak istemci sayısı
CLIENT_RUNTIME=10     # Her istemcinin çalışacağı süre (saniye)

echo "Starting chat server..."
# Sunucuyu arka planda başlat (eğer zaten çalışmıyorsa)
# Bu satırı sunucunuzu nasıl başlattığınıza göre düzenleyin.
# Örneğin: ./chatserver $SERVER_PORT &
./chatserver $SERVER_PORT &
SERVER_PID=$! # Sunucu PID'sini sakla
sleep 2 # Sunucunun başlaması için biraz bekle

echo "Starting $NUM_CLIENTS chat clients..."

for i in $(seq 1 $NUM_CLIENTS); do
    USERNAME="user$i"
    echo "Starting client $i with username: $USERNAME"
    # chatclient'ı arka planda başlat ve giriş için kullanıcı adını otomatik gönder
    (echo "$USERNAME"; sleep $CLIENT_RUNTIME) | ./chatclient $SERVER_IP $SERVER_PORT &
    CLIENT_PIDS[$i]=$! # İstemci PID'lerini sakla
    sleep 0.1 # İstemcilerin sırayla başlaması için küçük bir gecikme
done

echo "All clients started. Running for $CLIENT_RUNTIME seconds..."
sleep $((CLIENT_RUNTIME + 5)) # Tüm istemcilerin ve mesajların işlenmesi için bekle

echo "Killing client processes..."
for PID in "${CLIENT_PIDS[@]}"; do
    if kill -0 $PID 2>/dev/null; then # İşlemin hala çalışıp çalışmadığını kontrol et
        kill $PID
    fi
done

echo "Killing server process (PID: $SERVER_PID)..."
if kill -0 $SERVER_PID 2>/dev/null; then # İşlemin hala çalışıp çalışmadığını kontrol et
    kill $SERVER_PID
fi

echo "Test completed."
