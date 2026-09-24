@echo off
rem Abre o jogo (single-player). Rode a partir de qualquer pasta.
cd /d "%~dp0.."
build\flare.exe --data-path="%CD%" %*
