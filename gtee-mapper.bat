@echo off
rem Starts the GTee IO Bridge button mapper (gtee-mapper.ps1) without changing PowerShell's execution policy.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0gtee-mapper.ps1"
