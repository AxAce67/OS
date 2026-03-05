param(
    [string]$Host = "127.0.0.1",
    [int]$Port = 4545,
    [int]$PollMs = 300,
    [int]$MaxChars = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Sanitize-Clip {
    param(
        [string]$Text,
        [int]$Limit
    )
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ""
    }
    $t = $Text -replace "(\r\n|\r|\n)+", " "
    $t = $t -replace "\t+", " "
    $t = $t.Trim()
    if ($t.Length -gt $Limit) {
        $t = $t.Substring(0, $Limit)
    }
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $t.Length; $i++) {
        $code = [int][char]$t[$i]
        if ($code -ge 32 -and $code -le 126) {
            [void]$sb.Append($t[$i])
        } else {
            [void]$sb.Append("?")
        }
    }
    return $sb.ToString().Trim()
}

$lastSent = ""
$client = $null
$stream = $null
$writer = $null

try {
    while ($true) {
        try {
            if ($null -eq $client -or -not $client.Connected) {
                if ($null -ne $writer) { $writer.Dispose(); $writer = $null }
                if ($null -ne $stream) { $stream.Dispose(); $stream = $null }
                if ($null -ne $client) { $client.Close(); $client = $null }
                $client = New-Object System.Net.Sockets.TcpClient
                $client.Connect($Host, $Port)
                $stream = $client.GetStream()
                $writer = New-Object System.IO.StreamWriter($stream, [System.Text.Encoding]::ASCII)
                $writer.AutoFlush = $true
            }

            $raw = Get-Clipboard -Raw -ErrorAction SilentlyContinue
            $clip = Sanitize-Clip -Text $raw -Limit $MaxChars
            if (-not [string]::IsNullOrWhiteSpace($clip) -and $clip -ne $lastSent) {
                $writer.Write("CLIP:")
                $writer.WriteLine($clip)
                $lastSent = $clip
            }
        } catch {
            if ($null -ne $writer) { $writer.Dispose(); $writer = $null }
            if ($null -ne $stream) { $stream.Dispose(); $stream = $null }
            if ($null -ne $client) { $client.Close(); $client = $null }
        }
        Start-Sleep -Milliseconds $PollMs
    }
}
finally {
    if ($null -ne $writer) { $writer.Dispose() }
    if ($null -ne $stream) { $stream.Dispose() }
    if ($null -ne $client) { $client.Close() }
}
