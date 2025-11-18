// agente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "protocolo.h"

#define MAX_NOMBRE 64
#define MAX_PIPE   128

static void uso(const char *prog) {
    fprintf(stderr, "Uso: %s -s nombreAgente -a archivoSolicitudes -p pipeControlador\n", prog);
    exit(EXIT_FAILURE);
}

static void error_fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int opt;
    char nombreAgente[MAX_NOMBRE] = {0};
    char archivoSolicitudes[256] = {0};
    char pipeControlador[MAX_PIPE] = {0};
    int tieneS=0,tieneA=0,tieneP=0;

    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
        case 's':
            strncpy(nombreAgente, optarg, MAX_NOMBRE - 1);
            tieneS = 1;
            break;
        case 'a':
            strncpy(archivoSolicitudes, optarg, sizeof(archivoSolicitudes) - 1);
            tieneA = 1;
            break;
        case 'p':
            strncpy(pipeControlador, optarg, MAX_PIPE - 1);
            tieneP = 1;
            break;
        default:
            uso(argv[0]);
        }
    }

    if (!tieneS || !tieneA || !tieneP) {
        uso(argv[0]);
    }

    // Crear pipe de respuesta del agente
    char pipeResp[MAX_PIPE];
    snprintf(pipeResp, sizeof(pipeResp), "pipe_resp_%s", nombreAgente);
    unlink(pipeResp);
    if (mkfifo(pipeResp, 0666) == -1) {
        error_fatal("mkfifo pipeResp");
    }

    // Abrir pipe del controlador para escritura
    int fdCtrl = open(pipeControlador, O_WRONLY);
    if (fdCtrl == -1) {
        error_fatal("open pipeControlador");
    }

    // Enviar registro: REG|agente|pipeResp
    char registro[MAXLINE];
    snprintf(registro, sizeof(registro), "REG|%s|%s\n", nombreAgente, pipeResp);
    write(fdCtrl, registro, strlen(registro));

    // Abrir pipe de respuesta para lectura
    int fdResp = open(pipeResp, O_RDONLY);
    if (fdResp == -1) {
        error_fatal("open pipeResp");
    }

    // Leer hora actual enviada por el controlador
    char buffer[MAXLINE];
    int horaActual = 0;
    ssize_t n = read(fdResp, buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        // Esperamos algo como: HORA|8
        if (strncmp(buffer, "HORA|", 5) == 0) {
            horaActual = atoi(buffer + 5);
            printf("** Agente %s registrado. Hora actual de simulacion: %d\n",
                   nombreAgente, horaActual);
        } else {
            printf("Respuesta inesperada del controlador: %s\n", buffer);
        }
    } else {
        printf("No se recibio hora inicial del controlador.\n");
    }

    // Abrir archivo de solicitudes
    FILE *f = fopen(archivoSolicitudes, "r");
    if (!f) {
        perror("fopen archivoSolicitudes");
        close(fdResp);
        close(fdCtrl);
        unlink(pipeResp);
        exit(EXIT_FAILURE);
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), f)) {
        // Formato: Familia,hora,personas
        char *familia = strtok(linea, ",\n");
        char *horaStr = strtok(NULL, ",\n");
        char *persStr = strtok(NULL, ",\n");

        if (!familia || !horaStr || !persStr) {
            printf("Linea invalida en archivo: %s\n", linea);
            continue;
        }

        int hora = atoi(horaStr);
        int personas = atoi(persStr);

        if (hora < horaActual) {
            printf("** Solicitud no enviada (hora %d < hora actual %d) para familia %s\n",
                   hora, horaActual, familia);
            continue;
        }

        // Enviar solicitud: REQ|agente|familia|hora|personas
        char solicitud[MAXLINE];
        snprintf(solicitud, sizeof(solicitud),
                 "REQ|%s|%s|%d|%d\n", nombreAgente, familia, hora, personas);
        write(fdCtrl, solicitud, strlen(solicitud));
        printf("** Enviada solicitud: agente=%s, familia=%s, hora=%d, personas=%d\n",
               nombreAgente, familia, hora, personas);

        // Esperar respuesta
        n = read(fdResp, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("** Respuesta controlador: %s\n", buffer);
        } else {
            printf("No se recibio respuesta del controlador para la solicitud.\n");
        }

        sleep(2); // Esperar 2 segundos antes de la siguiente
    }

    printf("Agente %s termina.\n", nombreAgente);

    fclose(f);
    close(fdResp);
    close(fdCtrl);
    unlink(pipeResp);

    return 0;
}
