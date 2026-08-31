# Write meta.json (id/version/dll/sha256) next to the packaged dll.
# Usage: make_meta.ps1 <stageDir> <dllPath>
param(
    [string]$StageDir,
    [string]$DllPath
)
$name = [System.IO.Path]::GetFileName($DllPath)
$sha = [System.Security.Cryptography.SHA256]::Create()
$fs = [System.IO.File]::OpenRead($DllPath)
$bytes = $sha.ComputeHash($fs)
$fs.Close()
$sb = New-Object System.Text.StringBuilder
foreach ($b in $bytes) { [void]$sb.Append($b.ToString("x2")) }
$hash = $sb.ToString()
$meta = @{
    id      = "hci.demo.pkg"
    version = "1.0.0"
    dll     = $name
    sha256  = $hash
} | ConvertTo-Json
[System.IO.File]::WriteAllText(
    (Join-Path $StageDir "meta.json"),
    $meta + "`n",
    (New-Object System.Text.UTF8Encoding($false))
)
Write-Host "meta.json written: id=hci.demo.pkg dll=$name sha256=$hash"