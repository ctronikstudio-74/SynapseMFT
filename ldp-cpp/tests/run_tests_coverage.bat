@echo off
REM Script per eseguire i test e generare il coverage (solo su sistemi compatibili)

mkdir build
cd build
cmake -DCOVERAGE=ON ..
cmake --build .

REM Esegui i test
ctest --output-on-failure

REM Genera il report coverage (richiede gcovr)
gcovr -r .. --html --html-details -o coverage.html
cd ..
