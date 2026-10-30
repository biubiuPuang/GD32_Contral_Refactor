$ErrorActionPreference = 'Stop'

try {
    $projectDir = Split-Path -Parent $PSScriptRoot
    $projectPath = Join-Path $projectDir 'GD32E230C8T6.uvprojx'
    $logPath = Join-Path $PSScriptRoot 'keil-debug-build.log'
    $uv4Path = 'D:\_Keil5\UV4\UV4.exe'

    if (!(Test-Path -LiteralPath $uv4Path)) {
        throw "Keil executable not found: $uv4Path"
    }
    if (Test-Path -LiteralPath $logPath) {
        Remove-Item -LiteralPath $logPath
    }
    $uvArguments = @('-b', ('"{0}"' -f $projectPath), '-j0',
                     '-t', '"Target 1"', '-o', ('"{0}"' -f $logPath))
    $buildProcess = Start-Process -FilePath $uv4Path -ArgumentList $uvArguments `
        -WorkingDirectory $projectDir -WindowStyle Hidden -Wait -PassThru
    if (!(Test-Path -LiteralPath $logPath)) {
        throw "Keil did not create its build log (exit $($buildProcess.ExitCode))."
    }
    $buildLog = Get-Content -LiteralPath $logPath -Raw
    Write-Output $buildLog
    # UV4 exit 1 means warnings; exit 2 or higher means a build failure.
    if ($buildProcess.ExitCode -notin @(0, 1) -or
        $buildLog -notmatch '(?m)\b0 Error\(s\),\s*\d+ Warning\(s\)') {
        throw "Keil build failed (exit $($buildProcess.ExitCode)); debugging cancelled."
    }
    foreach ($suffix in @('axf', 'hex')) {
        $outputPath = Join-Path $projectDir "Objects\GD32E230C8T6.$suffix"
        if (!(Test-Path -LiteralPath $outputPath)) {
            throw "Build output not found: $outputPath"
        }
    }
    # Never let a debug download overwrite the last two configuration pages.
    $hexPath = Join-Path $projectDir 'Objects\GD32E230C8T6.hex'
    [long]$addressBase = 0
    foreach ($line in Get-Content -LiteralPath $hexPath) {
        if (!$line.StartsWith(':') -or $line.Length -lt 11) {
            throw 'Invalid Intel HEX record.'
        }
        $count = [Convert]::ToInt32($line.Substring(1, 2), 16)
        $offset = [Convert]::ToInt32($line.Substring(3, 4), 16)
        $recordType = [Convert]::ToInt32($line.Substring(7, 2), 16)
        if ($recordType -eq 4) {
            $addressBase = [Convert]::ToInt64($line.Substring(9, 4), 16) * 65536
        } elseif ($recordType -eq 2) {
            $addressBase = [Convert]::ToInt64($line.Substring(9, 4), 16) * 16
        } elseif ($recordType -eq 0 -and $count -gt 0) {
            $address = $addressBase + $offset
            if ($address -lt 0x08000000 -or $address + $count -gt 0x0800F800) {
                throw ('HEX data overlaps reserved configuration or non-Flash memory: 0x{0:X8}' -f $address)
            }
        }
    }
    Write-Output 'Keil build and Flash range check passed. No board was programmed by this build task.'
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
