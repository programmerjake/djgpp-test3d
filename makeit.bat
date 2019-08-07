cls
if "%1"=="" redir -eo gcc -msse4 test3d.c -o test3d.exe -O2 -Wall  
@shift
if not "%0"=="" redir -eo gcc test3d.c -o test3d.exe -Wall %0 %1 %2 %3 %4 %5 %6 %7 %8 %9
