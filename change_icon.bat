@echo off
reshacker\ResourceHacker.exe -open "dist\Greenhouse.exe" -save "dist\Greenhouse_new.exe" -action addoverwrite -res "app\icon.ico" -mask ICONGROUP,1,
del "dist\Greenhouse.exe"
rename "dist\Greenhouse_new.exe" "Greenhouse.exe" 