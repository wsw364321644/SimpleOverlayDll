call .\gen64.bat
cmake --build .\build64\ --config RelWithDebInfo
call .\gen32.bat
cmake --build .\build32\ --config RelWithDebInfo

