param([ValidateSet('dev','profile','release')][string]$Preset = 'dev')
. "$PSScriptRoot/environment.ps1"
Push-Location $taskProjectRoot
try {
    $taskSources = Get-ChildItem src -Recurse -Filter '*.cpp' | Where-Object Name -NE 'third_party.cpp'
    foreach ($taskSource in $taskSources) {
        & clang-tidy $taskSource.FullName -p "build/$Preset"
        if ($LASTEXITCODE -ne 0) { throw "Analysis failed: $($taskSource.Name)" }
    }
} finally { Pop-Location }

