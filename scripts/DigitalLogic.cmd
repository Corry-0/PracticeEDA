@echo off
rem Prefer the packaged executable; source checkouts fall back to the Release build.
if exist "%~dp0PracticeEDA.exe" (
    start "" "%~dp0PracticeEDA.exe" --logic
) else (
    start "" "%~dp0..\build\Release\PracticeEDA.exe" --logic
)
