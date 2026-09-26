foreach ($f in @('D:\XLab\XProjects\esp-projects\tmp\test.c', 'D:\XLab\XProjects\esp-projects\tmp\test.o')) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
}
