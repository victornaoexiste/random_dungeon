@echo off
:: Random Dungeon - libera o jogo no Firewall do Windows para jogar em rede local.
:: Precisa rodar como administrador (este arquivo pede sozinho).
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
netsh advfirewall firewall delete rule name="Random Dungeon" >nul 2>&1
netsh advfirewall firewall add rule name="Random Dungeon" dir=in action=allow program="%~dp0RandomDungeon.exe" enable=yes profile=any >nul
netsh advfirewall firewall add rule name="Random Dungeon" dir=in action=allow protocol=UDP localport=4650-4651 enable=yes profile=any >nul
if %errorlevel% equ 0 (
    echo.
    echo  Pronto! O Random Dungeon esta liberado no firewall.
    echo  Agora seus amigos na mesma rede conseguem achar e entrar na sua partida.
) else (
    echo.
    echo  Nao consegui criar a regra do firewall.
)
echo.
pause
