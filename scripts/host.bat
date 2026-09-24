@echo off
rem Hospeda uma partida online na porta 4650 (ou %1). O host precisa estar dentro do jogo antes dos clientes conectarem.
cd /d "%~dp0.."
set PORT=%1
if "%PORT%"=="" set PORT=4650
build\flare.exe --data-path="%CD%" --net-host=%PORT%
