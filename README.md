# TP D: Aplicación Distribuida con BSD Sockets

Este proyecto implementa un medidor de throughput y latencia usando BSD sockets con un cliente y servidor que se comunican mediante TCP y UDP.

## Estructura del Proyecto

```
├── client.cpp          # Aplicación cliente
├── server.cpp          # Aplicación servidor
├── include/
│   └── common.h        # Definiciones comunes
├── ayuda/
│   └── handle_result.c # Funciones auxiliares (referencia)
├── Makefile           # Script de compilación
└── README.md          # Este archivo
```

## Requisitos

- Sistema operativo Unix/Linux/macOS
- Compilador GCC con soporte para C99/C++11
- Biblioteca pthread

## Compilación

Para compilar ambas aplicaciones:
```bash
make all
```

Para compilar solo el servidor:
```bash
make server
```

Para compilar solo el cliente:
```bash
make client
```

Para limpiar los archivos compilados:
```bash
make clean
```

## Uso

### 1. Ejecutar el Servidor

El servidor debe ejecutarse primero y permanecerá escuchando en los puertos configurados:

```bash
./server
```

El servidor escuchará en:
- Puerto TCP 20251: Para pruebas de download
- Puerto TCP 20252: Para pruebas de upload  
- Puerto UDP 20251: Para medición de latencia

### 2. Ejecutar el Cliente

Una vez que el servidor esté ejecutándose, ejecute el cliente especificando la IP del servidor:

```bash
./client <IP_del_servidor>
```

Ejemplo para prueba local:
```bash
./client 127.0.0.1
```

### 3. Prueba Automatizada

Para ejecutar una prueba completa automática:
```bash
make test
```

Esto iniciará el servidor en segundo plano, ejecutará el cliente, y luego detendrá el servidor.

## Funcionamiento

El cliente ejecuta las siguientes fases secuencialmente:

1. **Fase Previa**: Medición de latencia inicial usando UDP
2. **Fase Download**: 
   - Establece N conexiones TCP concurrentes al puerto 20251
   - Recibe datos durante T segundos (configurable, por defecto 20s)
   - Mide latencia durante la transferencia
3. **Fase Upload**:
   - Genera un ID de prueba aleatorio de 4 bytes
   - Establece N conexiones TCP concurrentes al puerto 20252
   - Envía ID de prueba + ID de conexión en cada conexión
   - Envía datos durante T segundos
   - Mide latencia durante la transferencia
4. **Reporte**: Envía resultados en formato JSON vía UDP a Logstash

## Configuración

Los parámetros se pueden modificar en `include/common.h`:

- `TEST_DURATION_SEC`: Duración de las pruebas (por defecto: 20 segundos)
- `NUM_CONN`: Número de conexiones concurrentes (por defecto: 10)
- `TCP_PORT_DOWNLOAD`: Puerto TCP para download (20251)
- `TCP_PORT_UPLOAD`: Puerto TCP para upload (20252)
- `UDP_PORT`: Puerto UDP para latencia (20251)
- `LOGSTASH_HOST`: IP del servidor Logstash (127.0.0.1)
- `LOGSTASH_PORT`: Puerto del servidor Logstash (5044)

## Protocolo

### Download Test
1. Cliente establece N conexiones TCP al puerto 20251
2. Servidor envía datos continuamente por T segundos
3. Cliente cuenta bytes recibidos y mide tiempo real
4. Servidor mantiene conexión T+3 segundos adicionales si cliente no cierra

### Upload Test  
1. Cliente genera ID de prueba de 4 bytes (primer byte ≠ 0xFF)
2. Cliente establece N conexiones TCP al puerto 20252
3. Por cada conexión, cliente envía:
   - 4 bytes: ID de prueba (network byte order)
   - 2 bytes: ID de conexión (network byte order)
   - Datos continuos por T segundos
4. Servidor recibe y consume datos
5. Servidor mantiene conexión T+3 segundos adicionales si cliente no cierra

### Latency Test
- Cliente envía datagramas UDP al puerto 20251
- Servidor hace echo de los datagramas recibidos
- Cliente mide tiempo de ida y vuelta

## Formato de Resultados JSON

```json
{
  "timestamp": "1640995200",
  "test_id": "0x12345678",
  "server_ip": "192.168.1.100",
  "test_duration_sec": 20,
  "num_connections": 10,
  "download_throughput_mbps": 850.25,
  "upload_throughput_mbps": 720.50,
  "total_download_bytes": 2147483648,
  "total_upload_bytes": 1800000000,
  "latency_pre_test": {
    "min_ms": 1.20,
    "max_ms": 3.45,
    "avg_ms": 2.10,
    "success_rate": 100.00
  },
  "latency_during_download": {
    "min_ms": 1.50,
    "max_ms": 4.20,
    "avg_ms": 2.80,
    "success_rate": 100.00
  },
  "latency_during_upload": {
    "min_ms": 1.30,
    "max_ms": 3.80,
    "avg_ms": 2.40,
    "success_rate": 100.00
  }
}
```

## Compatibilidad

Este cliente y servidor están diseñados para interoperar con implementaciones de otros grupos que sigan el mismo protocolo especificado en los requisitos del TP.

## Troubleshooting

### Error "Address already in use"
Si el servidor no puede iniciar debido a puertos en uso:
```bash
# Verificar qué proceso está usando el puerto
lsof -i :20251
lsof -i :20252

# Terminar procesos si es necesario
pkill -f server
```

### Error de permisos
En algunos sistemas puede ser necesario permisos para usar ciertos puertos:
```bash
sudo ./server
```

### Conexión rechazada
Verificar que:
1. El servidor esté ejecutándose
2. No haya firewall bloqueando los puertos
3. La IP del servidor sea accesible desde el cliente