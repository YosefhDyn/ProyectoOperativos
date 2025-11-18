// protocolo.h
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#define MAXLINE 256
#define HORA_MIN 7
#define HORA_MAX 19

typedef struct {
    int reservadas;
} HoraInfo;

typedef struct {
    int horaIni, horaFin;
    int segHoras;
    int aforo;
    HoraInfo horas[HORA_MAX + 1];

    int solicitudes_aceptadas;
    int solicitudes_reprog;
    int solicitudes_negadas;
} ControlState;

#endif
