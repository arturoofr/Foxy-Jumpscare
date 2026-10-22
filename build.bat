@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist release mkdir release
rc.exe /nologo /fo src\resource.res src\resource.rc
cl.exe /nologo /O1 /Os /GL /GS- /Gy /GR- /EHs-c- /MD /W3 /DNDEBUG src\main.cpp src\resource.res /Fe:release\FoxyJumpscare.exe /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
del /q *.obj src\*.res 2>nul
copy /y assets\foxyjumpscare.mp4 release\foxyjumpscare.mp4 >nul
copy /y assets\foxyicon.ico release\foxyicon.ico >nul
echo Build completed: release\FoxyJumpscare.exe
