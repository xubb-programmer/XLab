$src = 'D:\XLab\XProjects\esp-projects\src'
if (Test-Path -LiteralPath $src) {
    $resolved = (Resolve-Path -LiteralPath $src).Path
    if ($resolved -ne 'D:\XLab\XProjects\esp-projects\src') {
        throw "unexpected src path: $resolved"
    }
    Get-ChildItem -LiteralPath $resolved -Force | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Recurse -Force
        Write-Output "removed $($_.FullName)"
    }
}
