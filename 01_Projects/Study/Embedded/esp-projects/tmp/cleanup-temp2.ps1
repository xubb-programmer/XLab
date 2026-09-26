$targets = @(
  'D:\XLab\XProjects\esp-projects\11_oled',
  'D:\XLab\XProjects\esp-projects\cleanup-temp.ps1',
  'D:\XLab\XProjects\esp-projects\install-idf55.ps1',
  'D:\XLab\XProjects\doc\_find_display.py',
  'D:\XLab\XProjects\doc\_dump_pages.py',
  'D:\XLab\XProjects\doc\_pages_217_234.txt'
)
foreach ($t in $targets) {
    if (-not (Test-Path -LiteralPath $t)) { continue }
    $resolved = (Resolve-Path -LiteralPath $t).Path
    if ($resolved -like 'D:\XLab\XProjects\*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
        Write-Output "removed $resolved"
    }
}
