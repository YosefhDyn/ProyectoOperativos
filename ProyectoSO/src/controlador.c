// controlador.c
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "protocolo.h"

#define MAX_RESERVAS 1024
#define MAX_AGENTES  64
#define MAX_NOMBRE   64
#define MAX_PIPE     128

typedef struct {
    char familia[MAX_NOMBRE];
    int personas;
    int horaInicio; // hora de inicio de la reserva (dura 2 horas)
    int activa;
} Reserva;

typedef struct {
    char nombreAgente[MAX_NOMBRE];
    char pipeRespuesta[MAX_PIPE];
    int enUso;
} AgenteInfo;

ControlState estado;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Estado adicional
static Reserva reservas[MAX_RESERVAS];
static AgenteInfo agentes[MAX_AGENTES];
static int horaActual;

// Utilidades
static void error_fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void inicializar_estado() {
    for (int h = 0; h <= HORA_MAX; ++h) {
        estado.horas[h].reservadas = 0;
    }
    for (int i = 0; i < MAX_RESERVAS; ++i) {
        reservas[i].activa = 0;
    }
    for (int i = 0; i < MAX_AGENTES; ++i) {
        agentes[i].enUso = 0;
    }
    horaActual = estado.horaIni;
    estado.solicitudes_aceptadas = 0;
    estado.solicitudes_reprog = 0;
    estado.solicitudes_negadas = 0;
}

// Busca un agente por nombre
static AgenteInfo *buscar_agente(const char *nombre) {
    for (int i = 0; i < MAX_AGENTES; ++i) {
        if (agentes[i].enUso && strcmp(agentes[i].nombreAgente, nombre) == 0) {
            return &agentes[i];
        }
    }
    return NULL;
}

static AgenteInfo *registrar_agente(const char *nombre, const char *pipeResp) {
    AgenteInfo *a = buscar_agente(nombre);
    if (a) {
        strncpy(a->pipeRespuesta, pipeResp, MAX_PIPE - 1);
        a->pipeRespuesta[MAX_PIPE - 1] = '\0';
        return a;
    }
    for (int i = 0; i < MAX_AGENTES; ++i) {
        if (!agentes[i].enUso) {
            agentes[i].enUso = 1;
            strncpy(agentes[i].nombreAgente, nombre, MAX_NOMBRE - 1);
            agentes[i].nombreAgente[MAX_NOMBRE - 1] = '\0';
            strncpy(agentes[i].pipeRespuesta, pipeResp, MAX_PIPE - 1);
            agentes[i].pipeRespuesta[MAX_PIPE - 1] = '\0';
            return &agentes[i];
        }
    }
    return NULL;
}

static int ocupacion_en_hora(int hora) {
    int total = 0;
    for (int i = 0; i < MAX_RESERVAS; ++i) {
        if (reservas[i].activa) {
            if (hora >= reservas[i].horaInicio &&
                hora < reservas[i].horaInicio + 2) {
                total += reservas[i].personas;
            }
        }
    }
    return total;
}

static int hay_cupo_bloque(int horaInicio, int personas) {
    if (horaInicio < estado.horaIni || horaInicio + 1 >= estado.horaFin)
        return 0;
    for (int h = horaInicio; h < horaInicio + 2; ++h) {
        if (ocupacion_en_hora(h) + personas > estado.aforo)
            return 0;
    }
    return 1;
}

static Reserva *crear_reserva(const char *familia, int personas, int horaInicio) {
    for (int i = 0; i < MAX_RESERVAS; ++i) {
        if (!reservas[i].activa) {
            reservas[i].activa = 1;
            reservas[i].personas = personas;
            reservas[i].horaInicio = horaInicio;
            strncpy(reservas[i].familia, familia, MAX_NOMBRE - 1);
            reservas[i].familia[MAX_NOMBRE - 1] = '\0';
            // actualizar ocupación por hora
            for (int h = horaInicio; h < horaInicio + 2; ++h) {
                if (h >= HORA_MIN && h <= HORA_MAX) {
                    estado.horas[h].reservadas += personas;
                }
            }
            return &reservas[i];
        }
    }
    return NULL;
}

// Enviar mensaje de texto simple al pipe de respuesta de un agente
static void enviar_respuesta(const char *pipeResp, const char *mensaje) {
    int fd = open(pipeResp, O_WRONLY);
    if (fd == -1) {
        fprintf(stderr, "No se pudo abrir pipe de respuesta %s: %s\n",
                pipeResp, strerror(errno));
        return;
    }
    write(fd, mensaje, strlen(mensaje));
    close(fd);
}

static void procesar_registro(char *agente, char *pipeResp) {
    pthread_mutex_lock(&lock);
    AgenteInfo *info = registrar_agente(agente, pipeResp);
    int hora = horaActual;
    pthread_mutex_unlock(&lock);

    if (!info) {
        fprintf(stderr, "No se pudo registrar agente %s\n", agente);
        return;
    }

    char buff[128];
    snprintf(buff, sizeof(buff), "HORA|%d", hora);
    enviar_respuesta(info->pipeRespuesta, buff);
    printf("** Agente registrado: %s, pipeResp=%s, horaActual=%d\n",
           agente, info->pipeRespuesta, hora);
}

static void procesar_solicitud(char *agente, char *familia, int horaSolic, int personas) {
    AgenteInfo *info = buscar_agente(agente);
    if (!info) {
        fprintf(stderr, "Solicitud de agente no registrado: %s\n", agente);
        return;
    }

    char respuesta[256];

    pthread_mutex_lock(&lock);
    int horaAct = horaActual;

    // Validaciones básicas
    if (personas > estado.aforo) {
        estado.solicitudes_negadas++;
        pthread_mutex_unlock(&lock);
        snprintf(respuesta, sizeof(respuesta),
                 "RESP|NEGADA_AFORO|%s|%d|%d|Personas > aforo",
                 familia, horaSolic, personas);
        enviar_respuesta(info->pipeRespuesta, respuesta);
        return;
    }

    if (horaSolic < estado.horaIni || horaSolic > estado.horaFin) {
        estado.solicitudes_negadas++;
        pthread_mutex_unlock(&lock);
        snprintf(respuesta, sizeof(respuesta),
                 "RESP|NEGADA_FUERA_RANGO|%s|%d|%d|Hora fuera del rango de simulacion",
                 familia, horaSolic, personas);
        enviar_respuesta(info->pipeRespuesta, respuesta);
        return;
    }

    if (horaSolic < horaAct) {
        // extemporánea: buscar reprogramación
        int nuevaHora = -1;
        for (int h = horaAct; h < estado.horaFin; ++h) {
            if (hay_cupo_bloque(h, personas)) {
                nuevaHora = h;
                break;
            }
        }
        if (nuevaHora == -1) {
            estado.solicitudes_negadas++;
            pthread_mutex_unlock(&lock);
            snprintf(respuesta, sizeof(respuesta),
                     "RESP|NEGADA_EXTEMPORANEA|%s|%d|%d|No hay cupo posterior",
                     familia, horaSolic, personas);
            enviar_respuesta(info->pipeRespuesta, respuesta);
            return;
        } else {
            crear_reserva(familia, personas, nuevaHora);
            estado.solicitudes_reprog++;
            pthread_mutex_unlock(&lock);
            snprintf(respuesta, sizeof(respuesta),
                     "RESP|REPROGRAMADA|%s|%d|%d|Reprogramada a %d-%d",
                     familia, horaSolic, personas, nuevaHora, nuevaHora + 2);
            enviar_respuesta(info->pipeRespuesta, respuesta);
            return;
        }
    }

    // Hora futura o actual
    if (horaSolic >= estado.horaFin) {
        estado.solicitudes_negadas++;
        pthread_mutex_unlock(&lock);
        snprintf(respuesta, sizeof(respuesta),
                 "RESP|NEGADA_DIA_COMPLETO|%s|%d|%d|Debe volver otro dia",
                 familia, horaSolic, personas);
        enviar_respuesta(info->pipeRespuesta, respuesta);
        return;
    }

    if (hay_cupo_bloque(horaSolic, personas)) {
        crear_reserva(familia, personas, horaSolic);
        estado.solicitudes_aceptadas++;
        pthread_mutex_unlock(&lock);
        snprintf(respuesta, sizeof(respuesta),
                 "RESP|ACEPTADA|%s|%d|%d|Reserva OK %d-%d",
                 familia, horaSolic, personas, horaSolic, horaSolic + 2);
        enviar_respuesta(info->pipeRespuesta, respuesta);
        return;
    } else {
        // Buscar otra franja
        int nuevaHora = -1;
        for (int h = horaSolic + 1; h < estado.horaFin; ++h) {
            if (hay_cupo_bloque(h, personas)) {
                nuevaHora = h;
                break;
            }
        }
        if (nuevaHora == -1) {
            estado.solicitudes_negadas++;
            pthread_mutex_unlock(&lock);
            snprintf(respuesta, sizeof(respuesta),
                     "RESP|NEGADA_SINCUPO|%s|%d|%d|No hay bloques disponibles",
                     familia, horaSolic, personas);
            enviar_respuesta(info->pipeRespuesta, respuesta);
            return;
        } else {
            crear_reserva(familia, personas, nuevaHora);
            estado.solicitudes_reprog++;
            pthread_mutex_unlock(&lock);
            snprintf(respuesta, sizeof(respuesta),
                     "RESP|REPROGRAMADA|%s|%d|%d|Reprogramada a %d-%d",
                     familia, horaSolic, personas, nuevaHora, nuevaHora + 2);
            enviar_respuesta(info->pipeRespuesta, respuesta);
            return;
        }
    }
}

static void imprimir_movimientos_hora(int hora) {
    // hora es la horaActual después de avanzar
    int entran = 0, salen = 0;
    printf("**************************************************\n");
    printf("** Hora actual: %d:00\n", hora);
    printf("  Familias que salen:\n");
    for (int i = 0; i < MAX_RESERVAS; ++i) {
        if (reservas[i].activa && reservas[i].horaInicio + 2 == hora) {
            printf("    - %s (%d personas)\n", reservas[i].familia, reservas[i].personas);
            salen += reservas[i].personas;
            // la reserva ya terminó completamente
            reservas[i].activa = 0;
        }
    }
    printf("  Familias que entran:\n");
    for (int i = 0; i < MAX_RESERVAS; ++i) {
        if (reservas[i].activa && reservas[i].horaInicio == hora) {
            printf("    + %s (%d personas)\n", reservas[i].familia, reservas[i].personas);
            entran += reservas[i].personas;
        }
    }
    int ocup = ocupacion_en_hora(hora);
    printf("  Total salen: %d, total entran: %d, ocupacion actual: %d\n",
           salen, entran, ocup);
}

// Hilo del reloj
void *hilo_reloj(void *arg) {
    while (1) {
        sleep(estado.segHoras);

        pthread_mutex_lock(&lock);
        if (horaActual >= estado.horaFin) {
            pthread_mutex_unlock(&lock);
            break;
        }
        horaActual++;
        int hora = horaActual;
        imprimir_movimientos_hora(hora);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

// Reporte final
static void imprimir_reporte_final() {
    printf("\n===== REPORTE FINAL =====\n");
    int maxOcup = -1, minOcup = 1000000;
    printf("Ocupacion por hora:\n");
    for (int h = estado.horaIni; h < estado.horaFin; ++h) {
        int ocup = estado.horas[h].reservadas;
        printf("  Hora %d: %d personas\n", h, ocup);
        if (ocup > maxOcup) maxOcup = ocup;
        if (ocup < minOcup) minOcup = ocup;
    }
    printf("Horas pico: ");
    for (int h = estado.horaIni; h < estado.horaFin; ++h) {
        int ocup = estado.horas[h].reservadas;
        if (ocup == maxOcup) printf("%d ", h);
    }
    printf("\nHoras de menor ocupacion: ");
    for (int h = estado.horaIni; h < estado.horaFin; ++h) {
        int ocup = estado.horas[h].reservadas;
        if (ocup == minOcup) printf("%d ", h);
    }
    printf("\nSolicitudes aceptadas: %d\n", estado.solicitudes_aceptadas);
    printf("Solicitudes reprogramadas: %d\n", estado.solicitudes_reprog);
    printf("Solicitudes negadas: %d\n", estado.solicitudes_negadas);
    printf("=========================\n");
}

// Parseo de argumentos
static void uso(const char *prog) {
    fprintf(stderr,
            "Uso: %s -i horaIni -f horaFin -s segHoras -t aforo -p pipeRecibe\n",
            prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int opt;
    char pipeRecibe[MAX_PIPE] = {0};
    int tieneI=0,tieneF=0,tieneS=0,tieneT=0,tieneP=0;

    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
        case 'i':
            estado.horaIni = atoi(optarg);
            tieneI = 1;
            break;
        case 'f':
            estado.horaFin = atoi(optarg);
            tieneF = 1;
            break;
        case 's':
            estado.segHoras = atoi(optarg);
            tieneS = 1;
            break;
        case 't':
            estado.aforo = atoi(optarg);
            tieneT = 1;
            break;
        case 'p':
            strncpy(pipeRecibe, optarg, MAX_PIPE - 1);
            tieneP = 1;
            break;
        default:
            uso(argv[0]);
        }
    }

    if (!tieneI || !tieneF || !tieneS || !tieneT || !tieneP) {
        uso(argv[0]);
    }

    if (estado.horaIni < HORA_MIN || estado.horaIni > HORA_MAX ||
        estado.horaFin < HORA_MIN || estado.horaFin > HORA_MAX ||
        estado.horaIni >= estado.horaFin) {
        fprintf(stderr, "Rango de horas invalido. Debe estar entre %d y %d y horaIni < horaFin.\n",
                HORA_MIN, HORA_MAX);
        exit(EXIT_FAILURE);
    }
    if (estado.segHoras <= 0 || estado.aforo <= 0) {
        fprintf(stderr, "segHoras y aforo deben ser positivos.\n");
        exit(EXIT_FAILURE);
    }

    inicializar_estado();

    // Crear pipe nominal si no existe
    unlink(pipeRecibe);
    if (mkfifo(pipeRecibe, 0666) == -1) {
        error_fatal("mkfifo");
    }

    int fd = open(pipeRecibe, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        error_fatal("open pipeRecibe");
    }

    printf("Controlador iniciado. Rango %d-%d, aforo=%d, segHoras=%d, pipe=%s\n",
           estado.horaIni, estado.horaFin, estado.aforo, estado.segHoras, pipeRecibe);

    pthread_t thReloj;
    if (pthread_create(&thReloj, NULL, hilo_reloj, NULL) != 0) {
        error_fatal("pthread_create");
    }

    char buffer[MAXLINE];
    while (1) {
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            // Puede haber varias lineas en el buffer
            char *line = strtok(buffer, "\n");
            while (line) {
                if (strncmp(line, "REG|", 4) == 0) {
                    // REG|agente|pipeResp
                    char *token = strtok(line + 4, "|");
                    char *nombreAgente = token;
                    token = strtok(NULL, "|");
                    char *pipeResp = token;
                    if (nombreAgente && pipeResp) {
                        procesar_registro(nombreAgente, pipeResp);
                    }
                } else if (strncmp(line, "REQ|", 4) == 0) {
                    // REQ|agente|familia|hora|personas
                    char *token = strtok(line + 4, "|");
                    char *nombreAgente = token;
                    char *familia = NULL;
                    char *horaStr = NULL;
                    char *persStr = NULL;
                    if (token) {
                        token = strtok(NULL, "|");
                        familia = token;
                    }
                    if (token) {
                        token = strtok(NULL, "|");
                        horaStr = token;
                    }
                    if (token) {
                        token = strtok(NULL, "|");
                        persStr = token;
                    }
                    if (nombreAgente && familia && horaStr && persStr) {
                        int hora = atoi(horaStr);
                        int personas = atoi(persStr);
                        procesar_solicitud(nombreAgente, familia, hora, personas);
                    }
                }
                line = strtok(NULL, "\n");
            }
        } else {
            // Si no hay datos, dormir un poquito
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(100000); // 0.1s
            }
        }

        pthread_mutex_lock(&lock);
        int terminado = (horaActual >= estado.horaFin);
        pthread_mutex_unlock(&lock);
        if (terminado) break;
    }

    pthread_join(thReloj, NULL);
    close(fd);
    unlink(pipeRecibe);

    imprimir_reporte_final();

    return 0;
}