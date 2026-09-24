@echo off
rem Conecta em um host. Uso: scripts\join.bat IP[:PORTA]   (padrao 127.0.0.1:4650)
cd /d "%~dp0.."
set TARGET=%1
if "%TARGET%"=="" set TARGET=127.0.0.1:4650
build\flare.exe --data-path="%CD%" --net-join=%TARGET%
