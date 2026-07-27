@echo off

sdcc -V --use-non-free -mpic14 -p16f628a ProtoClock.c
echo -----------------
echo erro: %errorlevel%
echo ----------------
if %errorlevel% neq 0 goto erro

gplink -O 2 -f 0 -r -w -m -s C:\Programas\gputils\lkr\16f628a_g.lkr -o ProtoClock.hex ProtoClock.o C:\Programas\SDCC\lib\pic14\libsdcc.lib C:\Programas\SDCC\non-free\lib\pic14\pic16f628a.lib

rem del *.asm
rem del *.map
rem del *.o
rem del *.lst
rem del *.cod


goto fim

:erro
echo erro de compilacao
pause 
:fim