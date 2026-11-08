@echo off
cd /d "%~dp0"
echo ======================================================================
echo Pushing roblox-launcher to toichoichess-hash/vietnameseprivateserver...
echo ======================================================================
echo If a browser window appears, click "Sign in with your browser" or "Authorize".
echo.
git push -u origin main
if %errorlevel% equ 0 (
    echo.
    echo [✓] Successfully pushed to https://github.com/toichoichess-hash/vietnameseprivateserver!
) else (
    echo.
    echo [!] Push failed or authorization was cancelled.
)
pause

