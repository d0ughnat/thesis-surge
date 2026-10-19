@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 tests\protection_test.cpp /Febuild\protection_test.exe /Fobuild\protection_test.obj
if errorlevel 1 exit /b 1
build\protection_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /Itests\support /I"C:\Users\PC\Documents\Arduino\libraries\ArduinoJson\src" tests\storage_test.cpp /Febuild\storage_test.exe /Fobuild\storage_test.obj
if errorlevel 1 exit /b 1
build\storage_test.exe
if errorlevel 1 exit /b 1
python tests\export_test.py
