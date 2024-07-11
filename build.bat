call .\gen64.bat
cmake --build .\build64\ --config Release
call .\gen32.bat
cmake --build .\build32\ --config Release

