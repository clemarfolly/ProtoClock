@echo off

set "PATH=%PATH%;C:\Programas\SDCC\bin;C:\Programas\gputils\bin"

set SRC=ProtoClock84_Otimizado

sdcc -S --use-non-free -mpic14 -p16f84 %SRC%.c
if %errorlevel% neq 0 goto erro

gpasm -I C:\Programas\gputils\header -o %SRC%.o -c -q %SRC%.asm
if %errorlevel% neq 0 goto erro

gplink -O 2 -w -q -f 0x00 -r -m -s C:\Programas\gputils\lkr\16f84_g.lkr -o %SRC%.hex %SRC%.o C:\Programas\SDCC\lib\pic14\libsdcc.lib C:\Programas\SDCC\non-free\lib\pic14\pic16f84.lib
if %errorlevel% neq 0 goto erro

python analise_memoria.py %SRC%.map

del *.o
del *.lst
del *.cod
del *.asm
del *.map

goto fim

:erro
echo ERRO na compilacao!

:fim
