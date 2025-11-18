# Sistema de Reservas de Parque - Proyecto Sistemas Operativos

## Descripción

Sistema de simulación de reservas para un parque usando procesos concurrentes, hilos POSIX y comunicación mediante pipes nominales. El sistema permite a múltiples agentes solicitar reservas de 2 horas para grupos familiares, mientras un controlador central gestiona la disponibilidad y el aforo del parque.

## Características Principales

- **Arquitectura Cliente-Servidor**: Controlador (servidor) y Agentes (clientes)
- **Comunicación**: Pipes nominales POSIX
- **Concurrencia**: Hilos POSIX para simulación de tiempo
- **Sincronización**: Mutexes para protección de datos compartidos
- **Validación completa**: Manejo de errores en llamadas al sistema

## Estructura del Proyecto

```
ProyectoSO/
├── src/
│   ├── protocolo.h        # Constantes y estructuras compartidas
│   ├── controlador.c      # Servidor - Controlador de Reservas
│   └── agente.c           # Cliente - Agente de Reservas
├── bin/                   # Ejecutables compilados
├── inputs/                # Archivos CSV con solicitudes
│   ├── agente1.csv
│   ├── agente2.csv
│   └── agente3.csv
├── Makefile               # Configuración de compilación
├── ejecutar.sh            # Script de ejecución automática
└── README.md              # Este archivo
```

## Compilación

### Requisitos
- GCC con soporte C11
- Sistema operativo Linux
- Biblioteca POSIX threads (pthread)

### Comandos de compilación

```bash
# Compilar todo el proyecto
make

# Limpiar archivos compilados
make clean

# Compilar solo el controlador
make bin/controlador

# Compilar solo el agente
make bin/agente
```

## Ejecución

### Opción 1: Ejecución Automática (Recomendada)

```bash
./ejecutar.sh
```

Este script:
1. Compila el proyecto
2. Limpia pipes anteriores
3. Inicia el controlador
4. Ejecuta los 3 agentes automáticamente
5. Muestra toda la salida del sistema
6. Limpia recursos al terminar

### Opción 2: Ejecución Manual

**Terminal 1 - Controlador:**
```bash
./bin/controlador -i 8 -f 19 -s 2 -t 50 -p pipe_controlador
```

**Terminales 2, 3, 4 - Agentes:**
```bash
./bin/agente -s Agente1 -a inputs/agente1.csv -p pipe_controlador
./bin/agente -s Agente2 -a inputs/agente2.csv -p pipe_controlador
./bin/agente -s Agente3 -a inputs/agente3.csv -p pipe_controlador
```

## Parámetros del Controlador

```
./bin/controlador -i horaIni -f horaFin -s segHoras -t total -p pipeRecibe
```

| Parámetro | Descripción | Rango/Formato |
|-----------|-------------|---------------|
| `-i horaIni` | Hora inicial de simulación | 7-19 (formato 24h) |
| `-f horaFin` | Hora final de simulación | 7-19 (formato 24h) |
| `-s segHoras` | Segundos por hora simulada | > 0 |
| `-t total` | Aforo máximo del parque | > 0 |
| `-p pipeRecibe` | Nombre del pipe para comunicación | Cadena de texto |

**Ejemplo:**
```bash
./bin/controlador -i 8 -f 19 -s 2 -t 50 -p pipe_controlador
```

## Parámetros del Agente

```
./bin/agente -s nombre -a archivoSolicitudes -p pipeControlador
```

| Parámetro | Descripción | Formato |
|-----------|-------------|---------|
| `-s nombre` | Nombre del agente | Cadena de texto |
| `-a archivoSolicitudes` | Archivo CSV con solicitudes | Ruta al archivo |
| `-p pipeControlador` | Pipe del controlador | Mismo que el controlador |

**Ejemplo:**
```bash
./bin/agente -s Agente1 -a inputs/agente1.csv -p pipe_controlador
```

## Formato de Archivos CSV

Los archivos de solicitudes tienen el formato:

```
Familia,Hora,Personas
```

**Ejemplo (inputs/agente1.csv):**
```
Zuluaga,8,10
Dominguez,8,4
Rojas,10,10
Fin,0,0
```

- **Familia**: Nombre del grupo familiar
- **Hora**: Hora de inicio deseada (7-19)
- **Personas**: Cantidad de personas en el grupo
- **Fin,0,0**: Marca el final del archivo (obligatorio)

## Funcionamiento del Sistema

### 1. Inicio del Controlador
- Inicializa estructuras de datos
- Crea el pipe nominal de recepción
- Inicia el hilo del reloj de simulación
- Espera conexiones de agentes

### 2. Registro de Agentes
- El agente se conecta al controlador
- Envía mensaje de registro con su nombre y pipe de respuesta
- El controlador responde con la hora actual de simulación

### 3. Procesamiento de Solicitudes
- Los agentes leen su archivo CSV línea por línea
- Envían solicitudes de reserva (2 horas por familia)
- Esperan respuesta del controlador
- Pausa de 2 segundos entre solicitudes

### 4. Tipos de Respuesta

#### a) Reserva Aceptada
```
RESP|ACEPTADA|Familia|Hora|Personas|Reserva OK hora_inicio-hora_fin
```

#### b) Reserva Reprogramada
```
RESP|REPROGRAMADA|Familia|Hora_Solicitada|Personas|Reprogramada a nueva_hora-nueva_hora+2
```

Casos de reprogramación:
- Solicitud extemporánea (hora ya pasó)
- No hay cupo en la hora solicitada

#### c) Reserva Negada
```
RESP|NEGADA_*|Familia|Hora|Personas|Razón
```

Razones de negación:
- **NEGADA_AFORO**: Grupo mayor que aforo máximo
- **NEGADA_FUERA_RANGO**: Hora fuera del rango de simulación
- **NEGADA_EXTEMPORANEA**: Hora pasada y sin cupo posterior
- **NEGADA_SIN_CUPO**: No hay cupo disponible

### 5. Simulación del Tiempo
- Cada `segHoras` segundos, avanza una hora simulada
- Imprime familias que entran y salen del parque
- Muestra ocupación actual

### 6. Reporte Final
Al finalizar la simulación, el controlador imprime:
- Ocupación por hora
- Horas pico (mayor ocupación)
- Horas de menor ocupación
- Cantidad de solicitudes aceptadas
- Cantidad de solicitudes reprogramadas
- Cantidad de solicitudes negadas

## Ejemplo de Salida

```
Controlador iniciado. Rango 8-19, aforo=50, segHoras=2, pipe=pipe_controlador
✅ Agente registrado: Agente1, pipeResp=pipe_resp_Agente1, horaActual=8
⏰ Agente Agente1 registrado. Hora actual de simulacion: 8
➡ Enviada solicitud: agente=Agente1, familia=Zuluaga, hora=8, personas=10
⬅ Respuesta controlador: RESP|ACEPTADA|Zuluaga|8|10|Reserva OK 8-10

⏰ Hora actual: 9:00
  Familias que salen:
  Familias que entran:
    + Perez (8 personas)
  Total salen: 0, total entran: 8, ocupacion actual: 18

===== REPORTE FINAL =====
Ocupacion por hora:
  Hora 8: 0 personas
  Hora 9: 0 personas
  ...
Horas pico: 10 11 
Horas de menor ocupacion: 8 15 16 17 18 
Solicitudes aceptadas: 5
Solicitudes reprogramadas: 1
Solicitudes negadas: 0
=========================
```

## Validaciones Implementadas

- ✅ Validación de parámetros de entrada
- ✅ Rango de horas (7-19)
- ✅ Aforo máximo por hora
- ✅ Solicitudes extemporáneas
- ✅ Manejo de errores en llamadas al sistema
- ✅ Cierre apropiado de recursos (pipes, archivos)
- ✅ Sincronización thread-safe con mutexes

## Notas Técnicas

### Comunicación con Pipes
- El controlador mantiene un pipe de lectura (pipeRecibe)
- Cada agente crea su propio pipe de respuesta (pipe_resp_NombreAgente)
- Protocolo de mensajes:
  - `REG|nombre|pipeRespuesta` - Registro de agente
  - `REQ|agente|familia|hora|personas` - Solicitud de reserva
  - `RESP|...` - Respuesta del controlador

### Sincronización
- Mutex global (`lock`) protege:
  - Hora actual de simulación
  - Arrays de reservas
  - Estadísticas del sistema
- Hilo separado para reloj de simulación

### Manejo de Recursos
- Todos los pipes se eliminan al finalizar
- Archivos se cierran correctamente
- Threads se joinean apropiadamente

## Autores

Proyecto de Sistemas Operativos 2025-30

## Licencia

Proyecto académico - Universidad
