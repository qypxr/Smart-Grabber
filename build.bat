@echo off
rem ------------------------------------------------------------
rem  Build YouTubeGrabber.exe (GUI) with Microsoft cl.exe
rem  Run from "x64 Native Tools Command Prompt for VS 2022",
rem  from THIS folder (where main.cpp lies).
rem ------------------------------------------------------------
cl /nologo /std:c++20 /O2 /EHsc /MT /utf-8 /DNDEBUG /I imgui /I . ^
   main.cpp imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
   imgui\imgui_impl_win32.cpp imgui\imgui_impl_dx11.cpp ^
   /Fe:YouTubeGrabber.exe /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)
del *.obj 2>nul
echo.
echo OK: YouTubeGrabber.exe is ready. Copy it to any folder and run it.
