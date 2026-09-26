Add-Type -AssemblyName System.IO.Compression.FileSystem

$jobs = @(
    @{
        zip  = 'C:\Espressif\dist\xtensa-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip'
        dest = 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119'
    },
    @{
        zip  = 'C:\Espressif\dist\riscv32-esp-elf-14.2.0_20241119-x86_64-w64-mingw32.zip'
        dest = 'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119'
    }
)

foreach ($j in $jobs) {
    # 只允许删除 C:\Espressif\tools 下的目标目录
    if (-not (Test-Path -LiteralPath $j.dest)) { throw "missing dest: $($j.dest)" }
    $resolved = (Resolve-Path -LiteralPath $j.dest).Path
    if ($resolved -notlike 'C:\Espressif\tools\*') { throw "unexpected dest: $resolved" }

    Remove-Item -LiteralPath $resolved -Recurse -Force
    New-Item -ItemType Directory -Path $resolved -Force | Out-Null

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $archive = [System.IO.Compression.ZipFile]::OpenRead($j.zip)
    $count = 0
    foreach ($entry in $archive.Entries) {
        $target = Join-Path $resolved ($entry.FullName -replace '/', '\')
        if ($entry.FullName.EndsWith('/')) {
            New-Item -ItemType Directory -Path $target -Force | Out-Null
        } else {
            $parent = Split-Path $target -Parent
            if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target)
            $count++
        }
    }
    $archive.Dispose()
    $sw.Stop()
    Write-Output "extracted $count files -> $resolved in $([math]::Round($sw.Elapsed.TotalSeconds,1))s"
}

Write-Output '--- verify cc1 ---'
Get-ChildItem 'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119' -Recurse -Filter 'cc1.exe' | Select-Object FullName
Get-ChildItem 'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119' -Recurse -Filter 'cc1.exe' | Select-Object FullName
