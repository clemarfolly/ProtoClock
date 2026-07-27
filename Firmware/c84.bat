@echo off

set "PATH=%PATH%;C:\Programas\SDCC\bin;C:\Programas\gputils\bin"

sdcc -S --use-non-free -mpic14 -p16f84 ProtoClock84.c
echo -----------------
echo erro: %errorlevel%
echo ----------------
if %errorlevel% neq 0 goto erro

gpasm -I C:\Programas\gputils\header -o ProtoClock84.o -c ProtoClock84.asm
echo -----------------
echo erro: %errorlevel%
echo ----------------
if %errorlevel% neq 0 goto erro

gplink -O 2 -f 0x00 -r -w -m -s C:\Programas\gputils\lkr\16f84_g.lkr -o ProtoClock84.hex ProtoClock84.o C:\Programas\SDCC\lib\pic14\libsdcc.lib C:\Programas\SDCC\non-free\lib\pic14\pic16f84.lib

 del *.asm
 del *.map
 del *.o
rem del *.lst
 del *.cod


goto fim

:erro
echo erro de compilacao
pause 
:fim
