$clock = 'D:\XLab\XProjects\esp-projects\src\clock'
foreach ($name in @('sdkconfig', 'README.md', '.vscode')) {
    $target = Join-Path $clock $name
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Write-Output "removed $target"
    }
}
