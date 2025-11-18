#!/bin/bash

echo "=== Compilando proyecto ==="
make clean
make

if [ $? -ne 0 ]; then
    echo "Error en la compilación"
    exit 1
fi

echo ""
echo "=== Limpiando pipes anteriores ==="
rm -f pipe_* 2>/dev/null

echo ""
echo "=== Iniciando controlador ==="
./bin/controlador -i 8 -f 19 -s 2 -t 50 -p pipe_controlador &
CONTROLADOR_PID=$!

sleep 1

echo ""
echo "=== Ejecutando agentes ==="
echo ""

./bin/agente -s Agente1 -a inputs/agente1.csv -p pipe_controlador &
AGENTE1_PID=$!

sleep 0.5

./bin/agente -s Agente2 -a inputs/agente2.csv -p pipe_controlador &
AGENTE2_PID=$!

sleep 0.5

./bin/agente -s Agente3 -a inputs/agente3.csv -p pipe_controlador &
AGENTE3_PID=$!

echo ""
echo "=== Sistema ejecutándose ==="
echo "Presiona Ctrl+C para detener"
echo ""

wait $CONTROLADOR_PID

echo ""
echo "=== Limpiando ==="
kill $AGENTE1_PID $AGENTE2_PID $AGENTE3_PID 2>/dev/null
rm -f pipe_* 2>/dev/null

echo "Terminado"
